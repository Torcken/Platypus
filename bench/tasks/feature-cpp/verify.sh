set -eu
c++ -std=c++20 -Dmain=fixture_main -c main.cpp -o main.o
cat > checker.cpp <<'CPP'
int square(int);
int main() { return square(7) == 49 && square(-3) == 9 && square(0) == 0 ? 0 : 1; }
CPP
c++ checker.cpp main.o -o verify-program
./verify-program
