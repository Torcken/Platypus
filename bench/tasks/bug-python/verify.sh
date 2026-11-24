set -eu
python3 -c "from calc import add; assert add(2, 3) == 5; assert add(-1, 1) == 0"
