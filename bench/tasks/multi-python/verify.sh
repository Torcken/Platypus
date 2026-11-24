set -eu
python3 -c "from config import DEFAULT_VALUE; from app import result; assert DEFAULT_VALUE == 7; assert result() == 14"
