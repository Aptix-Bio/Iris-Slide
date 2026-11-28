# RTM Bindings_01
"""Iris.Encoder's binding (Bindings_01; EncoderBindings_25.1, decided 2026-11-27).

The binding converts Python's arguments to the encoder's EncoderDesc, takes its defaults from the
core, and raises every refusal: ValueError for an argument it cannot convert, RuntimeError for a
Result the encoder returns failed. The encode itself is tested in C++ (tests/dicom_source_test.cpp);
these run against the installed module: cmake --install, then ctest in the Python build.
"""
import tempfile
import unittest
from pathlib import Path

from Iris.Encoder import (EncoderState, create_encoder, encode_slide_file, encoder_output_name)


class EncoderBindingTest(unittest.TestCase):
    def test_output_name_is_the_source_stem_and_encoding(self) -> None:
        self.assertEqual(encoder_output_name("slides/synthetic.dcm"), "synthetic.jpeg.iris")
        self.assertEqual(encoder_output_name("slides/synthetic.dcm", "av1"), "synthetic.av1.iris")

    # RTM Encode_13
    def test_unknown_encoding_is_a_value_error_naming_it(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaisesRegex(ValueError, '"webp"'):
                create_encoder(str(Path(folder) / "absent.dcm"), folder, encoding="webp")

    # RTM Encode_16
    def test_effort_above_six_and_jpeg_effort_are_refused_by_the_core(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaisesRegex(RuntimeError, "effort 7"):
                create_encoder(str(Path(folder) / "absent.dcm"), folder, encoding="av1", effort=7)
            with self.assertRaisesRegex(RuntimeError, "effort 2 is refused for TILE_ENCODING_JPEG"):
                create_encoder(str(Path(folder) / "absent.dcm"), folder, effort=2)

    # RTM Encode_13
    def test_av1_without_a_named_subsampling_takes_420(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            # The source is absent, so the refusal names it: every option, 4:2:0 included, passed.
            with self.assertRaisesRegex(RuntimeError, "absent.dcm"):
                create_encoder(str(Path(folder) / "absent.dcm"), folder, encoding="av1")

    # RTM Encode_13
    def test_av1_with_chroma_other_than_420_is_refused_by_the_core(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaisesRegex(RuntimeError, "4:2:0"):
                create_encoder(str(Path(folder) / "absent.dcm"), folder, encoding="av1", subsampling="444")

    def test_states_are_the_public_five(self) -> None:
        self.assertEqual([state.name for state in EncoderState.__members__.values()],
                         ["READY", "ACTIVE", "DONE", "FAILED", "INTERRUPTED"])

    def test_unknown_subsampling_is_a_value_error_naming_it(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaisesRegex(ValueError, '"411"'):
                create_encoder(str(Path(folder) / "absent.dcm"), folder, subsampling="411")

    def test_a_number_too_large_to_hold_is_refused_instead_of_wrapped(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaisesRegex(ValueError, "quality 65626"):
                create_encoder(str(Path(folder) / "absent.dcm"), folder, quality=65626)

    def test_a_failed_result_is_a_runtime_error_naming_the_source(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "absent.dcm"
            with self.assertRaisesRegex(RuntimeError, "absent.dcm"):
                create_encoder(str(source), folder)
            with self.assertRaisesRegex(RuntimeError, "absent.dcm"):
                encode_slide_file(str(source), folder, show_progress=False)

    def test_concurrency_zero_is_refused_by_the_core(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaisesRegex(RuntimeError, "concurrency 0"):
                create_encoder(str(Path(folder) / "absent.dcm"), folder, concurrency=0)


if __name__ == "__main__":
    unittest.main()
