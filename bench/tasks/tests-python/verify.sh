set -eu
python3 -m unittest discover -v && python3 -c "import test_calc, unittest; assert unittest.defaultTestLoader.loadTestsFromModule(test_calc).countTestCases() >= 3"
