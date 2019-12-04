#!/bin/sh
help(){
    echo ""
    echo "    Usage:"
    echo "       $0 <process_name> [parameters] [...]"
    echo ""
    exit 0
}

# 参数范围检查 -lt -> less than
if [ "$#" -lt 1 ]; 
then   
    help
fi   

# cmd=
# # 拼接带参数的命令
# for i in "$*"; do
#     if [ "${1}"="$i" ]
#     then
#         echo ${i}
#         #cmd="$cmd $i"      
#     fi                                                                                                     
# done 
cmd="$*"
#检查进程实例是否已经存在
while [ 1 ]; do
    PID=`pgrep ${1}` 
    if [ -z "$PID" ]
    then
        echo ""
        echo "restart process: [$cmd]" 
        exec ./${cmd} &
        echo "date: `date`"
        echo ""
    fi  
    #循环检测时间
    sleep 2
done
