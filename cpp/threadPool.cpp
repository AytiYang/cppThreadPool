#include "threadPool.h"


/// @brief 创建并初始化线程池
/// @param min 最小线程数
/// @param max 最大线程数
/// @param queueSize 队列容量
/// @return 线程池地址
threadPool* threadPoolCreate(int min,int max,int queueSize)
{
    threadPool* pool = (threadPool*)malloc(sizeof(threadPool));
    do
    {
    if(pool ==NULL)
    {
        printf("malloc threadpool fail\n");
        break;
    }
    pool->threadIDs = (pthread_t*)malloc(sizeof(pthread_t)*max);
    if(pool->threadIDs == NULL)
    {
        printf("malloc threadid fail\n");
        break;
    }
    memset(pool->threadIDs,0,sizeof(pthread_t)* max);   //这里指开max个线程的内存
    pool->minNum = min;
    pool->maxNum = max;
    pool->busyNum  = 0;
    pool->liveNum = min;
    pool->exitNum = 0;

    if(pthread_mutex_init(&pool->mutexPool,NULL) != 0 ||
       pthread_mutex_init(&pool->mutexBusy,NULL) != 0 ||
       pthread_cond_init(&pool->notEmpty,NULL) != 0 ||
       pthread_cond_init(&pool->notFull,NULL) != 0)
       {
            printf("mutex or condition init fail\n");
            break;
       }

    pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);
    pool->queueCapacity = queueSize;
    pool->queueSize = 0;
    pool->queueFront = 0;
    pool->queueRear = 0;

    pool->shutdownn = 0;

    //创建线程
    pthread_create(&pool->managerID,NULL,manager,pool);
    for(int i=0;i<min;i++)
    {
        pthread_create(&pool->threadIDs[i],NULL,worker,pool);
    }
    return pool;
    } while (0);
    //如果前面报错,这里回收那些已经创建的内存
    if(pool && pool->threadIDs)
    {
        free(pool->threadIDs);
    }
    if(pool &&pool->taskQ)
    {
        free(pool->taskQ);
    }
    if(pool)
    {
        free(pool);
    }
    return NULL;
}

int threadPoolDestory(threadPool *pool)
{
    if(pool== NULL)
    {
        return -1;
    }
    //关闭线程池
    pool->shutdownn = 1;

    // //yzh
    // for(int i=0;i<pool->liveNum;i++)
    // {
    //     if(pool->threadIDs[i])
    //     {
    //         pthread_join(pool->threadIDs[i],NULL);
    //     }
    // }

    // //yzh


    

    //阻塞回收管理者线程
    pthread_join(pool->managerID,NULL);

    //唤醒阻塞的消费者线程,唤醒后线程逻辑里判断shutdown会自动销毁
    for(int i=0;i<pool->liveNum;i++)
    {
        pthread_cond_signal(&pool->notEmpty);
    }

    for(int i=0;i<pool->liveNum;i++)
    {
        if(pool->threadIDs[i])
        {
            pthread_join(pool->threadIDs[i],NULL);
        }
        
    }

    if(pool->taskQ)
    {
        free(pool->taskQ);
        pool->taskQ = NULL;
    }
    if(pool->threadIDs)
    {
        free(pool->threadIDs);
        pool->threadIDs = NULL;
    }
    

    //销毁锁
    pthread_mutex_destroy(&pool->mutexPool);
    pthread_mutex_destroy(&pool->mutexBusy);
    
    pthread_cond_destroy(&pool->notEmpty);
    pthread_cond_destroy(&pool->notFull);

    free(pool);     //上面已经判断过了
    pool = NULL;

    return 0;
}

void threadPoolAdd(threadPool *pool, void (*func)(void *), void *arg)
{
    pthread_mutex_lock(&pool->mutexPool);
    //任务队列已满
    while(pool->queueSize == pool->queueCapacity && !pool->shutdownn)
    {
        //阻塞生产者线程
        pthread_cond_wait(&pool->notFull,&pool->mutexPool);
    }
    if(pool->shutdownn)
    {
        pthread_mutex_unlock(&pool->mutexPool);
        return;
    }

    //上述工作判断线程池是否关闭或已满

    //添加任务
    pool->taskQ[pool->queueRear].function = func;
    pool->taskQ[pool->queueRear].arg = arg;
    pool->queueRear = (pool->queueRear+1) % pool->queueCapacity;    //队尾后移
    pool->queueSize ++;

    //唤醒工作线程
    pthread_cond_signal(&pool->notEmpty);
    pthread_mutex_unlock(&pool->mutexPool);
}

int threadPoolBusyNum(threadPool *pool)
{
    pthread_mutex_lock(&pool->mutexBusy);
    int busyNum = pool->busyNum;
    pthread_mutex_unlock(&pool->mutexBusy);
    return busyNum;
}

int threadPoolLiveNum(threadPool *pool)
{
    pthread_mutex_lock(&pool->mutexPool);
    int liveNum = pool->liveNum;
    pthread_mutex_unlock(&pool->mutexPool);
    return liveNum;
}

/// @brief 工作线程,在pool的任务队列中取任务
/// @param arg 传进来pool对象的指针
/// @return 
void* worker(void* arg)
{
    threadPool* pool  = (threadPool*)arg;   //此处arg传进来应该是pool类型,所以要进行转换void* -->  threadPool*
    while(1)    //为什么不能用if,因为解除阻塞后可能还是为空,那就要继续阻塞,if的话阻塞完就默认队列不为空了
    {
        pthread_mutex_lock(&pool->mutexPool);
        //判断任务队列是否为空
        while(pool->queueSize == 0 && !pool->shutdownn)
        {
            //阻塞工作线程,wait自己会释放锁
            //not empty的作用是,当任务队列不为空时,程序继续往下运行
            pthread_cond_wait(&pool->notEmpty,&pool->mutexPool);


            //判断是否需要销毁线程
            if(pool->exitNum > 0)
            {
                pool->exitNum--;
                if(pool->liveNum > pool->minNum)
                {
                    pool->liveNum--;
                    //条件变量唤醒之后,会得到锁,所以这里要解锁
                    pthread_mutex_unlock(&pool->mutexPool);
                    threadExit(pool);
                }
                
            }

        }
        //判断线程池是否被关闭
        if(pool->shutdownn)
        {
            pthread_mutex_unlock(&pool->mutexPool);     //防止死锁
            threadExit(pool);
        }

        //从任务队列中取出一个任务
        Task task;
        task.function = pool->taskQ[pool->queueFront].function;  //此处为数组模拟队列
        task.arg = pool->taskQ[pool->queueFront].arg;
        //移动队头(下标),循环队列
        pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
        pool->queueSize --;
        pthread_cond_signal(&pool->notFull);
        pthread_mutex_unlock(&pool->mutexPool);
        
        //这里加锁是为了修改忙线程个数对应的变量
        //为什么分开两把锁,因为两把锁负责的内容不一样,如果共用一把锁,可以理解为只修改这么一点内容却占用了其他线程原本要修改大量内容的阻塞时间
        //mutexPool做的事情多,mutexBusy做的事情少

        printf("thread %ld start working...\n",pthread_self());
        pthread_mutex_lock(&pool->mutexBusy);
        
        pool->busyNum++;
        pthread_mutex_unlock(&pool->mutexBusy);
        task.function(task.arg);    
        free(task.arg);     //这里默认传进来的arg是堆内存而非栈内存,在这里进行释放(因为栈内存可能会被系统释放掉)
        task.arg = NULL;
        printf("thread %ld end working...\n", pthread_self());
        pthread_mutex_lock(&pool->mutexBusy);
        pool->busyNum--;
        
        //(*task.function)(task.arg);//这种写法也是对的,先解引用函数再传参
        pthread_mutex_unlock(&pool->mutexBusy);


    }
    return NULL;
}


/// @brief 管理者线程,增删工作线程
/// @param arg 传进来pool对象的指针
/// @return 
void *manager(void *arg)
{
    const int NUMBER = 2;
    threadPool *pool = (threadPool*)arg;
    while(!pool->shutdownn)
    {
        sleep(3);

        //取出线程池中任务数量 & 当前线程数量
        pthread_mutex_lock(&pool->mutexPool);
        int queueSize = pool->queueSize;
        int liveNum = pool->liveNum;
        pthread_mutex_unlock(&pool->mutexPool);

        //取出忙线程数量
        //为什么单独一把锁,因为busyNum访问比较频繁
        pthread_mutex_lock(&pool->mutexBusy);
        int busyNum = pool->busyNum;
        pthread_mutex_unlock(&pool->mutexBusy);

        //添加线程
        //任务个数 > 存活线程个数 && 存活线程数 < 最大线程数

        if(queueSize > liveNum && liveNum < pool->maxNum)
        {
            pthread_mutex_lock(&pool->mutexPool);
            int counter = 0;
            for(int i=0;i < pool->maxNum && counter < NUMBER && liveNum < pool->maxNum;i++)
            {
                
                if(pool->threadIDs[i]==0)
                {
                    pthread_create(&pool->threadIDs[i],NULL,worker,pool);
                    counter++;
                    pool->liveNum ++;
                }
                
            }
            pthread_mutex_unlock(&pool->mutexPool);
        }

        //销毁线程
        //忙线程 * 2 <存活线程 && 存活线程 > 最小线程数
        if(busyNum*2 < liveNum && liveNum > pool->minNum)
        {
            pthread_mutex_lock(&pool->mutexPool);
            //exitNum是用来给worker内部判断是否需要进行销毁用的
            pool->exitNum = NUMBER;
            pthread_mutex_unlock(&pool->mutexPool);
            
            //引导工作线程自杀
            for(int i=0;i<NUMBER;i++)
            {
                pthread_cond_signal(&pool->notEmpty);
            }
        }
    }
    return NULL;
}

void threadExit(threadPool *pool)
{
    pthread_t tid = pthread_self();
    for(int i=0;i<pool->maxNum;i++)
    {
        if(pool->threadIDs[i]==tid)
        {
            pool->threadIDs[i] = 0;
            printf("threadExit() called, %ld exiting...\n",tid);
            break;
        }
    }
    pthread_exit(NULL);
}
