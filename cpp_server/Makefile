# 定义编译器
CXX = g++

# 定义编译选项 (支持 C++11，并开启常见警告提示)
CXXFLAGS = -std=c++11 -Wall

# 编译生成的可执行文件名字
TARGET = game_server

# 【核心黑科技】：自动寻找当前目录下所有的 .cpp 文件！
SRCS = $(wildcard *.cpp)

# 默认执行的目标
all: $(TARGET)

# 真正的编译动作 (注意：下面这一行开头必须是一个 Tab 键！)
$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET)
	@echo "========================================"
	@echo "编译成功！老板，请直接输入 ./game_server 启动！"
	@echo "========================================"

# 清理生成的垃圾文件 (执行 make clean 时调用)
clean:
	rm -f $(TARGET)
	@echo "清理完毕！"
	