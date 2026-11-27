#!/usr/bin/env bash
# VcpkgCaching_2 (CacheIdentities_2.6; RTM Build_06): GitHub Actions' sign-in to Google Cloud
# for Iris-Slide's build caches, with no stored key. Run by a project owner from a terminal;
# every step reads before it creates, so a second run changes nothing and a changed rule here is
# applied by running it again. Every gcloud command names the project, since a machine's default
# project may be another one.
#
# The store is one bucket the Iris repositories may share, the cache bucket (repository variable
# IRIS_SLIDE_CACHE_BUCKET), created by the project's owner; this script fails, naming it, while it
# does not exist. Iris-Slide is public, so no committed file names the bucket, the project or the
# service accounts: each is a repository variable, given to this script in the environment and
# printed again at the end for `gh variable set`.
# Iris-Slide is public, so its identities reach only the objects under iris-slide/, by IAM
# conditions on the object name; a public repository's CI never reads another repository's
# compiled objects:
#   iris-slide/vcpkg/<triplet>/...   vcpkg's packages, written by cache.yml on main
#   iris-slide/sccache/...           the compiler's objects, written by cache.yml on main
#   iris-slide/pr/vcpkg/..., iris-slide/pr/sccache/...
#                                    the pull requests' quarantine, deleted after 7 days
#
# - An OIDC provider of its own in the project's existing workload identity pool, accepting only
#   Aptix-Bio/Iris-Slide's tokens, matched by repository ID and owner ID, since a repository name
#   can be reused. The pool itself is only read here. It may host other repositories' providers,
#   and a principal set names a pool attribute any provider in the pool could map, so Iris-Slide's
#   attribute names are prefixed iris_slide_ and no other provider can map them.
# - Two service accounts, the reader (IRIS_SLIDE_CACHE_READER) and the writer
#   (IRIS_SLIDE_CACHE_WRITER), each named by its full address in the project. The reader goes to every run of the repository, pull requests from the
#   repository's own branches included: it reads all of iris-slide/ and writes only under
#   iris-slide/pr/, so a pull request's job uploads each package and object as it builds them and a
#   failed job still speeds up the next, while main and releases never read what it wrote. The
#   writer goes only to cache.yml on refs/heads/main, by push or by hand: a pull request's job runs
#   its own build files before review, and vcpkg and sccache key an object by its inputs, so a
#   writer could plant an object under a key main will hit. Pull requests from forks get no OIDC
#   token from GitHub at all, and build without the store (.github/actions/caches).
# - A lifecycle rule deletes objects under iris-slide/pr/ 7 days after they were written, added to
#   the bucket's rules without removing any.
#
# Usage: PROJECT=<project id> BUCKET=<bucket> READER=<address> WRITER=<address> [POOL=<pool>] \
#            tools/cloud_federation.sh
#        PROJECT, BUCKET, READER and WRITER are the repository variables IRIS_SLIDE_GCP_PROJECT,
#        IRIS_SLIDE_CACHE_BUCKET, IRIS_SLIDE_CACHE_READER and IRIS_SLIDE_CACHE_WRITER; none has a
#        default. An account that does not exist is created in PROJECT from its address's name.
set -euo pipefail

PROJECT=${PROJECT:?cloud_federation: PROJECT is unset; name the Google Cloud project (the repository variable IRIS_SLIDE_GCP_PROJECT)}
BUCKET=${BUCKET:?cloud_federation: BUCKET is unset; name the cache bucket (the repository variable IRIS_SLIDE_CACHE_BUCKET)}
READER=${READER:?cloud_federation: READER is unset; give the address of the reader service account (the repository variable IRIS_SLIDE_CACHE_READER)}
WRITER=${WRITER:?cloud_federation: WRITER is unset; give the address of the writer service account (the repository variable IRIS_SLIDE_CACHE_WRITER)}
POOL=${POOL:-github}
PREFIX=iris-slide
PROVIDER=github-iris-slide
REPOSITORY=Aptix-Bio/Iris-Slide
REPOSITORY_ID=1470307364   # gh api repos/Aptix-Bio/Iris-Slide --jq .id, read 2026-11-24
OWNER_ID=353521923         # Aptix-Bio, the same call's .owner.id
WRITER_WORKFLOW="$REPOSITORY/.github/workflows/cache.yml@refs/heads/main"
project=(--project="$PROJECT")

# The APIs workload identity federation needs; enabling an enabled API changes nothing.
gcloud services enable iam.googleapis.com iamcredentials.googleapis.com sts.googleapis.com "${project[@]}"

number=$(gcloud projects describe "$PROJECT" --format='value(projectNumber)')
pool_path="projects/$number/locations/global/workloadIdentityPools/$POOL"

# The pool and the bucket are made by others; their absence stops the script before it creates anything.
if ! gcloud iam workload-identity-pools describe "$POOL" "${project[@]}" --location=global >/dev/null 2>&1; then
    echo "cloud_federation: no workload identity pool '$POOL' in $PROJECT" >&2
    exit 1
fi
if ! gcloud storage buckets describe "gs://$BUCKET" "${project[@]}" >/dev/null 2>&1; then
    echo "cloud_federation: no bucket gs://$BUCKET in $PROJECT: create it first" >&2
    exit 1
fi

# `^;^` makes `;` the list separator, since the CEL holds commas.
writer_cel="assertion.workflow_ref == '$WRITER_WORKFLOW' && assertion.ref == 'refs/heads/main'"
writer_cel+=" && (assertion.event_name == 'push' || assertion.event_name == 'workflow_dispatch') ? 'true' : 'false'"
mapping="^;^google.subject=assertion.sub;attribute.iris_slide_repository_id=assertion.repository_id"
mapping+=";attribute.iris_slide_cache_writer=$writer_cel"
condition="assertion.repository_id == '$REPOSITORY_ID' && assertion.repository_owner_id == '$OWNER_ID'"
provider=("${project[@]}" --location=global --workload-identity-pool="$POOL"
          --attribute-mapping="$mapping" --attribute-condition="$condition")
if gcloud iam workload-identity-pools providers describe "$PROVIDER" "${project[@]}" --location=global \
        --workload-identity-pool="$POOL" >/dev/null 2>&1; then
    gcloud iam workload-identity-pools providers update-oidc "$PROVIDER" "${provider[@]}"
else
    gcloud iam workload-identity-pools providers create-oidc "$PROVIDER" "${provider[@]}" \
        --issuer-uri="https://token.actions.githubusercontent.com" --display-name="GitHub Actions OIDC"
fi

# An address in another project is refused by the second describe, after the create made the name in PROJECT.
for account in "$READER" "$WRITER"; do
    if ! gcloud iam service-accounts describe "$account" "${project[@]}" >/dev/null 2>&1; then
        gcloud iam service-accounts create "${account%@*}" "${project[@]}" --display-name="${account%@*} ($REPOSITORY caches)"
    fi
    if ! gcloud iam service-accounts describe "$account" "${project[@]}" >/dev/null 2>&1; then
        echo "cloud_federation: no service account $account in $PROJECT; give an address in that project" >&2
        exit 1
    fi
done

bind_identity() {   # service account, principal set
    gcloud iam service-accounts add-iam-policy-binding "$1" "${project[@]}" \
        --role=roles/iam.workloadIdentityUser --member="principalSet://iam.googleapis.com/$pool_path/$2" \
        --condition=None >/dev/null
}
bind_identity "$READER" "attribute.iris_slide_repository_id/$REPOSITORY_ID"
bind_identity "$WRITER" "attribute.iris_slide_cache_writer/true"

# A service account just created is not yet known to Cloud Storage's IAM, which can answer 400
# "does not exist" for up to a minute. Each binding is retried a bounded number of times and fails
# loudly after the last.
bind_bucket() {   # role, service account, object prefix, condition title
    local attempt
    local expression="resource.name.startsWith('projects/_/buckets/$BUCKET/objects/$3')"
    for attempt in 1 2 3 4 5 6; do
        if gcloud storage buckets add-iam-policy-binding "gs://$BUCKET" "${project[@]}" --role="$1" \
                --member="serviceAccount:$2" \
                --condition="expression=$expression,title=$4" >/dev/null; then
            return 0
        fi
        echo "binding $2 on gs://$BUCKET refused (attempt $attempt of 6); IAM may still be propagating" >&2
        sleep 15
    done
    return 1
}
bind_bucket roles/storage.objectViewer "$READER" "$PREFIX/" "iris-slide read"
bind_bucket roles/storage.objectUser "$READER" "$PREFIX/pr/" "iris-slide pull-request quarantine"
bind_bucket roles/storage.objectUser "$WRITER" "$PREFIX/" "iris-slide read and write"

# The quarantine's 7-day rule, appended to the bucket's own rules when it is not already there.
lifecycle=$(mktemp)
trap 'rm -f "$lifecycle"' EXIT
gcloud storage buckets describe "gs://$BUCKET" "${project[@]}" --format=json \
  | python3 -c '
import json, sys
bucket = json.load(sys.stdin)
rules = bucket.get("lifecycle_config", {}).get("rule", [])
rule = {"action": {"type": "Delete"}, "condition": {"age": 7, "matchesPrefix": [sys.argv[1]]}}
changed = rule not in rules
json.dump({"rule": rules + [rule] if changed else rules}, open(sys.argv[2], "w"), indent=2)
sys.exit(0 if changed else 3)
' "$PREFIX/pr/" "$lifecycle" && status=0 || status=$?
case "$status" in
    0) gcloud storage buckets update "gs://$BUCKET" "${project[@]}" --lifecycle-file="$lifecycle" ;;
    3) echo "lifecycle: the $PREFIX/pr/ rule is already on gs://$BUCKET" ;;
    *) exit "$status" ;;
esac

echo "IRIS_SLIDE_GCP_PROVIDER=$pool_path/providers/$PROVIDER"
echo "IRIS_SLIDE_GCP_PROJECT=$PROJECT"
echo "IRIS_SLIDE_CACHE_BUCKET=$BUCKET"
echo "IRIS_SLIDE_CACHE_READER=$READER"
echo "IRIS_SLIDE_CACHE_WRITER=$WRITER"
