import pandas as pd
import multiprocessing
import time

def process_chunk(chunk):
    # Simulate heavy computation
    time.sleep(0.1)
    return chunk.sum()

def main():
    # Create a dummy large dataset
    data = pd.DataFrame({
        'id': range(1000),
        'value': [i * 0.5 for i in range(1000)]
    })
    
    # Currently sequential
    total_sum = 0
    for i in range(0, 1000, 250):
        chunk = data.iloc[i:i+250]
        total_sum += process_chunk(chunk)
    
    print(f"Total Sum: {total_sum}")

if __name__ == "__main__":
    main()
