set -eu
python3 -c "from calc import double, calculate; assert double(3) == 6; assert calculate(2, 4) == 12"
