#!/bin/bash
set -e

# 1. 编译
echo "=== 编译 ==="
cd /home/xixue/NexusCore/server/build
cmake .. > /dev/null
make -j$(nproc)

# 2. 上传二进制
echo "=== 上传二进制 ==="
scp /home/xixue/NexusCore/server/build/nexus_server root@8.134.18.58:/tmp/nexus_server

# 3. 上传 HTML 静态文件（排除 audio/，音频首次手动上传后不变）
echo "=== 上传 HTML ==="
ssh root@8.134.18.58 "mkdir -p /root/html/audio /root/html/css /root/html/js"
cd /home/xixue/NexusCore/client/html
for f in *.html css/* js/*; do
    scp "$f" root@8.134.18.58:/root/html/"$f"
done

# 4. 重启
echo "=== 重启 ==="
ssh root@8.134.18.58 "pkill nexus_server 2>/dev/null; sleep 1; mv /tmp/nexus_server /root/nexus_server; chmod +x /root/nexus_server; tmux send-keys -t nexus '/root/nexus_server --http-root /root/html/ --log /root/server.log' Enter"

echo "=== 完成 ==="

# 5. 日志
if [ "$1" = "--log" ]; then
    echo "=== 实时日志 (Ctrl+C 退出) ==="
    ssh root@8.134.18.58 -t "tmux attach -t nexus"
else
    sleep 1
    scp root@8.134.18.58:/root/server.log /home/xixue/NexusCore/docs/server.log
    echo "日志已同步到 docs/server.log"
fi
