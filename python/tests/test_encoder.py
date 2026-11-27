"""Iris.Encoder's binding (RTM Bindings_01; EncoderBindings_25.1, decided 2026-11-27).

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
