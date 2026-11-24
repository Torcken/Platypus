import unittest
import os
from bench.edit_formats import apply_search_replace, verify_file_content

class TestEditFormats(unittest.TestCase):
    def setUp(self):
        # Create a temporary file for testing
        self.test_file = "test_edit_file.txt"
        with open(self.test_file, "w") as f:
            f.write("original content\nline 2\nline 3")

    def tearDown(self):
        if os.path.exists(self.test_file):
            os.remove(self.test_file)

    def test_apply_search_replace_success(self):
        edits = [
            {
                "old_string": "original content",
                "new_string": "new content"
            }
        ]
        result = apply_search_replace(self.test_file, edits)
        self.assertTrue(result)
        
        with open(self.test_file, "r") as f:
            content = f.read()
        self.assertIn("new content", content)
        self.assertNotIn("original content", content)

    def test_apply_search_replace_not_found(self):
        # A missing search must fail without mutating the source.
        edits = [
            {
                "old_string": "non_existent",
                "new_string": "new_string"
            }
        ]
        result = apply_search_replace(self.test_file, edits)
        self.assertFalse(result)
        with open(self.test_file) as source:
            self.assertIn("original content", source.read())

    def test_verify_file_content_success(self):
        expected = "new content\nline 2\nline 3"
        # Manually set the file state
        with open(self.test_file, "w") as f:
            f.write(expected)
        
        result = verify_file_content(self.test_file, expected)
        self.assertTrue(result)

    def test_verify_file_content_failure(self):
        expected = "different content"
        with open(self.test_file, "w") as f:
            f.write("original content")
            
        result = verify_file_content(self.test_file, expected)
        self.assertFalse(result)

if __name__ == "__main__":
    unittest.main()
