import zmq
import struct
import time

class TradeSignal:
    # Make sure the format matches the C++ struct exactly
    FORMAT = '=32s8s16sdd?7x'  # = ensures standard size and alignment
    
    def __init__(self, symbol, side, type_, quantity, price):
        self.symbol = symbol.ljust(32, '\0').encode()
        self.side = side.ljust(8, '\0').encode()
        self.type = type_.ljust(16, '\0').encode()
        self.quantity = quantity
        self.price = price
        self.new_signal = True

    def pack(self):
        return struct.pack(
            self.FORMAT,
            self.symbol,
            self.side,
            self.type,
            self.quantity,
            self.price,
            self.new_signal
        )

class ZMQClient:
    def __init__(self, address="tcp://localhost:5556"):
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.PUB)
        self.socket.bind(address)
        time.sleep(2)  # Give some time for connection to establish

    def send_signal(self, symbol, side, type_, quantity, price):
        signal = TradeSignal(symbol, side, type_, quantity, price)
        packed_data = signal.pack()
        print(f"Sending signal: Size={len(packed_data)} bytes")
        print(f"Symbol: {symbol}, Side: {side}, Type: {type_}, Quantity: {quantity}, Price: {price}")
        self.socket.send(packed_data)

if __name__ == "__main__":
    client = ZMQClient()
    while True:
        client.send_signal("DOGEUSDT", "BUY", "LIMIT", 50, 0.25300)
        time.sleep(0.5)  # Send every 2 seconds
