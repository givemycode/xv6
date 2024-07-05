#include "kernel/types.h"
#include "user/user.h"
#define N 35

void solve(int* pl){
    int prime;
    //读取一个质数，如果没有数据了则直接返回
    if(read(pl[0], &prime, sizeof(prime)) == 0) return;
    printf("prime %d\n", prime); //打印输出
    int pr[2];
    pipe(pr);  //right管道

    int pid = fork();
    if(pid > 0){ //父进程
        close(pr[0]); //关闭right管道读取端
        //将当前质数的倍数筛出去，剩下的写入right管道写入端
        for(int t; read(pl[0], &t, sizeof(t)) != 0;){
            if(t % prime != 0)
                write(pr[1], &t, sizeof(t));
        }
        close(pr[1]); //关闭right管道写入端
        close(pl[0]); //关闭left管道读取端
        wait(0); //等待子进程完成
        exit(0); //正确退出

    }else if(pid == 0){
        close(pr[1]); //关闭right管道写入端
        close(pl[0]); //关闭left管道写入端
        solve(pr); //递归处理右邻
        close(pr[0]); //关闭right管道读取端
        exit(0); //正确退出
    }
    else{ //创建进程失败
        close(pl[0]);
        close(pr[1]);
        close(pr[0]);
        exit(1); //错误退出
    }
}

int main(int argc, char argv[]){
    int pl[2];  //lefts
    pipe(pl);  //创建left管道
    int pid = fork();
    if(pid > 0){ //主进程
        close(pl[0]);  //关闭left管道读取端
        for(int i =2 ; i <= N; ++i){ //将2-35写入left管道写入端
            write(pl[1], &i, sizeof(i));
        }
        close(pl[1]); //关闭left管道写入端
        wait(0);  //等待子进程完成
        exit(0);  //成功退出
    }else if(pid == 0){ //子进程
        close(pl[1]);  //关闭left管道写入端
        solve(pl);  //进行筛选
        close(pl[0]); //关闭left管道读取端
        exit(0); //正确退出
    }else{  //创建进程错误
        close(pl[1]);  
        close(pl[0]);
        exit(1);
    }
}
