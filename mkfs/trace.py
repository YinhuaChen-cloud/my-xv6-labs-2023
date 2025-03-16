# 定义要打断点的函数列表
TRACED = "bwrite balloc ialloc iappend rinode winode reset wsect".split()  
# 定义要忽略的变量
IGNORE = 'ip xp buf'.split()  

# trace 类继承 gdb.Breakpoint
class trace(gdb.Breakpoint):  
    def stop(self):
        # 获取当前调用帧和初始化调用栈信息列表
        f, bt = gdb.selected_frame(), []
        # 遍历调用栈
        while f and f.is_valid():  
            if (name := f.name()) in TRACED:  
                # 收集非忽略参数的值
                lvars = [f'{sym.name}={sym.value(f)}'  
                for sym in f.block()  
                if sym.is_argument and sym.name not in IGNORE]  
                # 格式化函数名（绿色）和参数，添加到调用栈
                bt.append(f'\033[32m{name}\033[0m({", ".join(lvars)})')  
            # 移动到调用栈的上一层
            f = f.older()  
        # 根据调用深度缩进打印当前函数信息
        print('    ' * (len(bt) - 1) + bt[0])  
        return False # won't stop at this breakpoint  

# 关闭GDB提示和分页，避免干扰输出。
gdb.execute('set prompt off')  
gdb.execute('set pagination off')  
# 为 TRACED 中的每个函数创建断点。
for fn in TRACED:  
    trace(fn)  
# 执行命令 run fs.img README user/_ls，启动被调试程序。
gdb.execute('run fs.img README user/_ls')  
# gdb 退出
gdb.execute('quit')  

