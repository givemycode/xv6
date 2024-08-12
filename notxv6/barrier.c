#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

static int nthread = 1;
static int round = 0;

struct barrier {
  // 定义了一个互斥锁变量barrier_mutex
  pthread_mutex_t barrier_mutex;
  // 定义了一个条件变量barrier_cond
  pthread_cond_t barrier_cond;
  // 表示参与同步的线程总数
  int nthread;      
  // 用于记录屏障的轮次
  int round;     // Barrier round
} bstate;

static void
barrier_init(void)
{
  // 初始化
  assert(pthread_mutex_init(&bstate.barrier_mutex, NULL) == 0);
  assert(pthread_cond_init(&bstate.barrier_cond, NULL) == 0);
  // 表示目前还没有线程到达屏障
  bstate.nthread = 0;
  bstate.round = 0;
}

// 每个线程在执行到屏障点时调用 barrier() 函数
static void barrier()
{
  // 获取互斥锁，确保对共享资源 bstate 的访问是互斥的
  pthread_mutex_lock(&bstate.barrier_mutex);
  if(++bstate.nthread == nthread) {
    // 表示所有线程已到达，进入下一轮同步
    bstate.round++;
    // 重置为 0，为下一轮屏障做准备
    bstate.nthread = 0;
    //  唤醒所有在条件变量上等待的线程
    pthread_cond_broadcast(&bstate.barrier_cond);  
  }
  else {
    // 不是所有线程都到达了屏障，当前线程需要等待
    // 等待期间，当前线程会释放互斥锁，允许其他线程进入临界区并可能成为第nthread个到达屏障的线程。
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  }
  pthread_mutex_unlock(&bstate.barrier_mutex);

  //所有线程在离开 barrier() 函数后继续它们的执行，此时它们已经同步，并且知道所有其他线程也已经通过了屏障。
}

static void *
thread(void *xa)
{
  long n = (long) xa;
  long delay;
  int i;

  for (i = 0; i < 20000; i++) {
    int t = bstate.round;
    assert (i == t);
    barrier();
    usleep(random() % 100);
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  long i;
  double t1, t0;

  if (argc < 2) {
    fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);

  barrier_init();

  for(i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, thread, (void *) i) == 0);
  }
  for(i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  printf("OK; passed\n");
}
