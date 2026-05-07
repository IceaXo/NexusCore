import socket
import threading
import time

# 这是一个模拟单个玩家的函数
def fake_player(player_id):
    try:
        # 1. 买电话，拨号连上 C++ 服务器
        client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client.connect(('127.0.0.1', 8080))
        
        # 收听欢迎语
        welcome = client.recv(1024).decode('utf-8')
        print(f"[玩家 {player_id} 进服] {welcome.strip()}")

        # 2. 像疯狗一样连续发 5 次移动指令
        for _ in range(5):
            client.sendall(b"W\n") # 发送大写 W 和回车
            report = client.recv(1024).decode('utf-8')
            # 故意不打印中间战报，防止终端被刷爆
            time.sleep(0.01) # 稍微喘口气，微秒级延迟

        # 3. 拔网线跑路
        client.close()
        print(f"[玩家 {player_id} 下线]")
        
    except Exception as e:
        print(f"玩家 {player_id} 掉线了: {e}")

print("=== 开始饱和式攻击 ===")

# 召唤 100 个并发线程（100 个玩家同时按门铃！）
threads = []
for i in range(100):
    t = threading.Thread(target=fake_player, args=(i,))
    threads.append(t)
    t.start()

# 等待所有玩家打完收工
for t in threads:
    t.join()

print("=== 攻击结束 ===")