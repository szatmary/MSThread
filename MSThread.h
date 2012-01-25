/*
All rights granted
The contents of this file have been placed into the public domain by the original copyright holder
Matthew Szatmary <szatmary@gmail.com>
No warranty is granted or implied.
*/

#ifndef MSTHREADS_H
#define MSTHREADS_H

// C++ wrapper for pthreads

/*
The primary object is MSProcessingQueue. The other objects are usable on their own, but were written to support MSProcessingQueue

Here is an example of how to use MSProcessingQueue

void threadFunc(void *param)
{
	while(1)
	{
		MSProcessingQueue<int> *q = ((MSProcessingQueue<int>*)(param));	
		int client = q->Pop();

		// Do something with the socket here;
	}
}

int main()
{
	// create a prcessing queue with 8 threads
	MSProcessingQueue<int> sockethandler(8);

	int sock = socket();
	listen(sock);
	while( client = accept(sock) )
	{
		sockethandler.Push(client);		
	}
}

*/


#include <pthread.h>
#include <time.h>
#include <queue>
#include <vector>

using namespace std;

#define TIMEOUT_INFINITE 0xFFFFFFFF

class MSMutex
{
private:
	pthread_mutex_t themutex;
public:
	 MSMutex();
	~MSMutex();

	bool Lock(unsigned long timeout = TIMEOUT_INFINITE);
	void Unlock();
};

class MSCondition
{
private:
	pthread_mutex_t themutex;
	pthread_cond_t  thecondition;
protected:
	bool _Lock(struct timespec *timeout);
	bool _Wait(struct timespec *timeout);
	bool _Signal();
	bool _Broadcast();
	void _Unlock();
public:
	MSCondition();
	virtual ~MSCondition();

	bool Wait(unsigned long timeout = TIMEOUT_INFINITE);
	bool Signal(unsigned long timeout = TIMEOUT_INFINITE);
	bool Broadcast(unsigned long timeout = TIMEOUT_INFINITE);
};

// do we need to change the semaphore so we can use it to protect data?
// kind of like we do in the queue below
class MSSemaphore : public MSCondition
{
private:
	long count;
public:
	MSSemaphore(unsigned long initialCount = 0);

	bool Obtain(unsigned long timeout = TIMEOUT_INFINITE);
	bool Release(unsigned long timeout = TIMEOUT_INFINITE);
	long Count(unsigned long timeout = TIMEOUT_INFINITE);
};

/* Thread safe queue */)
template<class T>
class MSQueue : public MSSemaphore
{
private:
	queue<T> *q;
public:
	 MSQueue();
	~MSQueue();

	bool Push(T item,unsigned long timeout = TIMEOUT_INFINITE);
	bool Push(vector<T> *items,unsigned long timeout = TIMEOUT_INFINITE);
	bool Pop (T *item,unsigned long timeout = TIMEOUT_INFINITE);
	bool Pop (vector<T> *items, long max, unsigned long timeout = TIMEOUT_INFINITE);
	long Count(unsigned long timeout = TIMEOUT_INFINITE);
};

typedef  void*(*MSThreadFunc)(void*);

class MSThread
{
private:
	pthread_t thread;
public:
	 MSThread(MSThreadFunc threadFunc, void *params);
	~MSThread();

	void *Join();
	void Cancel();
};

template<class T>
class MSProcessingQueue : public MSQueue<T>
{
private:
	vector<MSThread*> threads;
	MSMutex themutex;
	MSThreadFunc thefunc;
public:
	 MSProcessingQueue(unsigned long threadCount, MSThreadFunc threadFunc);
	~MSProcessingQueue();

	long ThreadCount();
	bool AddThreads(long count = 1);
	bool RemoveThreads(long count = 1);
};

///////////////////////////////////////////////////////
// Because a lot of this code is templated, It must be included inside the header file
///////////////////////////////////////////////////////

#define IS_TIMEOUT_INFINITE(tp) (tp->tv_sec == -1 && tp->tv_nsec == -1)
#define IS_TIMEOUT_ZERO(tp)     (tp->tv_sec ==  0 && tp->tv_nsec ==  0)


// WARNING!  WARNING!  WARNING!
// this is a known issue in pthreads in linux!
// If the clock is modified (by NTP or Daylight savings for example)
// The timespec returned by this function could refer to a time +/- the delta
// of the system clock change. Hence you may timeout (very) early or late
// do with that what you will
inline timespec MSTimeout2Timespec(unsigned long timeout)
{
	// this function converts the timeoutvalue to a realtime value in the future
	timespec tp;
	if ( 0 == timeout )
	{
		tp.tv_sec  = 0;
		tp.tv_nsec = 0;
		return tp;
	}
	
	if ( TIMEOUT_INFINITE == timeout )
	{
		tp.tv_sec  = -1;
		tp.tv_nsec = -1;
		return tp;		
	}

	clock_gettime(CLOCK_REALTIME, &tp);
	// not the fastest in the world, But we will never overflow
	// (at least not until 2038)

	// add seconds to tv_sec
	int seconds = (timeout / 1000);
	tp.tv_sec  += seconds;

	// and nanosecs to tv_nsec
	tp.tv_nsec += (timeout - (seconds * 1000));

	// if we pushed tv_nsec over 1 sec, carry it to tv_sec
	if ( 1000000000 < tp.tv_nsec )
	{
		tp.tv_sec  += 1;
		tp.tv_nsec -= 1000000000;
	}

	return tp;
}

///////////////////////////////////////////////////////
int
ms_pthread_mutex_lock(pthread_mutex_t *mutex, struct timespec *tp)
{
//	struct timespec tq;
//	clock_gettime(CLOCK_REALTIME, &tq);
//	cout << "mutex current " << tq.tv_sec << "." << tq.tv_nsec << endl;
//	cout << "mutex timeout " << tp->tv_sec << "." << tp->tv_nsec << endl;

	if(IS_TIMEOUT_INFINITE(tp))
		return pthread_mutex_lock(mutex);
	else
	if(IS_TIMEOUT_ZERO(tp))
		return pthread_mutex_trylock(mutex);
	else
		return pthread_mutex_timedlock(mutex,tp);
}

int
ms_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex, struct timespec *tp)
{
//	struct timespec tq;
//	clock_gettime(CLOCK_REALTIME, &tq);
//	cout << "cond current " << tq.tv_sec << "." << tq.tv_nsec << endl;
//	cout << "cond timeout " << tp->tv_sec << "." << tp->tv_nsec << endl;

	if(IS_TIMEOUT_INFINITE(tp))
		return pthread_cond_wait(cond,mutex);
	else	// will this work for 0 tmeout?
		return pthread_cond_timedwait(cond,mutex,tp);
}


///////////////////////////////////////////////////////

MSMutex::MSMutex()
{
	pthread_mutex_init(&themutex,0);
}

MSMutex::~MSMutex()
{
	pthread_mutex_destroy(&themutex);
}

bool
MSMutex::Lock(unsigned long timeout)
{
	struct timespec tp = MSTimeout2Timespec(timeout);
	return (0 == ms_pthread_mutex_lock(&themutex,&tp));
}

void
MSMutex::Unlock()
{
	pthread_mutex_unlock(&themutex);
}
///////////////////////////////////////////////////////

MSCondition::MSCondition()
{
	pthread_mutex_init(&themutex,0);
	pthread_cond_init(&thecondition,0);
};

MSCondition::~MSCondition()
{
	pthread_cond_destroy(&thecondition);
	pthread_mutex_destroy(&themutex);
}

bool
MSCondition::_Lock(struct timespec *timeout)
{
	return 0 != ms_pthread_mutex_lock(&themutex,timeout);
}

void
MSCondition::_Unlock()
{
	pthread_mutex_unlock(&themutex);
}

bool
MSCondition::_Wait(struct timespec *timeout)
{
	return 0 != ms_pthread_cond_wait(&thecondition,&themutex,timeout);
}

bool
MSCondition::_Signal()
{
	return 0 != pthread_cond_signal(&thecondition);
}

bool
MSCondition::_Broadcast()
{
	return 0 != pthread_cond_broadcast(&thecondition);
}

bool
MSCondition::Wait(unsigned long timeout)
{
	bool r = 0;
	struct timespec tp = MSTimeout2Timespec(timeout);
	if ( _Lock(&tp) )
	{
		r =  _Wait(&tp);
		_Unlock();
	}
	return r;
}

bool
MSCondition::Signal(unsigned long timeout)
{
	bool r = 0;
	struct timespec tp = MSTimeout2Timespec(timeout);
	if ( _Lock(&tp) )
	{
		r =  _Signal();
		_Unlock();
	}
	return r;
}

bool
MSCondition::Broadcast(unsigned long timeout)
{
	bool r = 0;
	struct timespec tp = MSTimeout2Timespec(timeout);
	if ( _Lock(&tp) )
	{
		r =  _Broadcast();
		_Unlock();
	}
	return r;
}
///////////////////////////////////////////////////////

MSSemaphore::MSSemaphore(unsigned long initialCount)
{
	count = initialCount;
};

bool
MSSemaphore::Obtain(unsigned long timeout)
{
	// if Cancel is called, this function is where the thread is killed
	// eaither on the call to pthread_testcancel() or ms_pthread_cond_wait()
	pthread_testcancel();

	struct timespec tp = MSTimeout2Timespec(timeout);
	
	if ( ! _Lock(&tp) )
	{
		return false;
	}

	while(!count)
	{
		// if Cancel is called, this it where the thread is killed
		if ( ! _Wait(&tp) )
		{
			_Unlock();
			return false;
		}
	}
	
	--count;
	_Unlock();
	return true;
}

bool
MSSemaphore::Release(unsigned long timeout)
{
	struct timespec tp = MSTimeout2Timespec(timeout);

	if ( ! _Lock(&tp) )
	{
		return false;
	}
	
	++count;
	if ( ! _Signal() )
	{
		// if this fails, we end up with items in the queue with nobody to pick them up
		_Unlock();
		return false;
	}

	_Unlock();
	return true;
}

long
MSSemaphore::Count(unsigned long timeout)
{
	struct timespec tp = MSTimeout2Timespec(timeout);
	if ( ! _Lock(&tp) )
	{
		return -1;
	}
	long c = count;
	_Unlock();
	return c;
}

///////////////////////////////////////////////////////

template<class T>
MSQueue<T>::MSQueue()
{
	q = new queue<T>;
};

template<class T>
MSQueue<T>::~MSQueue()
{
	delete q;
}

template<class T>
bool
MSQueue<T>::Pop(T *item,unsigned long timeout)
{
	// if Cancel is called, this function is where the thread is killed
	// eaither on the call to pthread_testcancel() or ms_pthread_cond_wait()
	pthread_testcancel();

	struct timespec tp = MSTimeout2Timespec(timeout);
	if ( ! _Lock(&tp) )
	{
		return false;
	}

	while (q->empty())
	{
		if ( _Wait(&tp) )
		{
			_Unlock();
			return false;
		}
	}
	
	*item = q->front();
	q->pop();
	_Unlock();
	return true;
}

template<class T>
bool
MSQueue<T>::Pop(vector<T> *items, long max, unsigned long timeout)
{
	// if Cancel is called, this function is where the thread is killed
	// eaither on the call to pthread_testcancel() or ms_pthread_cond_wait()
	pthread_testcancel();

	struct timespec tp = MSTimeout2Timespec(timeout);
	if ( ! _Lock(&tp) )
	{
		return false;
	}

	while (q->empty())
	{
		if ( 0 != _Wait(&tp) )
		{
			_Unlock();
			return false;
		}
	}
	
	while(max && !q->empty())
	{
		items->push_back( q->front() );
		q->pop();
		--max;
	}
	_Unlock();
	return true;
}

template<class T>
bool
MSQueue<T>::Push(T item,unsigned long timeout)
{
	struct timespec tp = MSTimeout2Timespec(timeout);

	if ( ! _Lock(&tp) )
		return false;
	
	q->push(item);
	bool r = _Signal();
	_Unlock();
	return r;
}

template<class T>
bool
MSQueue<T>::Push(vector<T> *items,unsigned long timeout)
{
	struct timespec tp = MSTimeout2Timespec(timeout);

	if ( ! _Lock(&tp) )
		return false;
	
	for(long i = items.size() ; i ; --i)
		q->push(items[i]);

	// I would assume broadcast would be better that calling signal X times
	bool r = _Broadcast();
	_Unlock();
	return r;
}

template<class T>
long
MSQueue<T>::Count(unsigned long timeout)
{
	struct timespec tp = MSTimeout2Timespec(timeout);

	if (!_Lock(&tp) )
		return -1;

	long c = q.size();
	_Unlock();
	return c;
}

///////////////////////////////////////////////////////

MSThread::MSThread(MSThreadFunc threadFunc, void *params)
{
	pthread_create(&thread,NULL,threadFunc,params);
	// TODO convert SIGs to cancels. Or Not?
	// pthread_signal_to_cancel_np()
}

MSThread::~MSThread()
{
	Join();
}

void
MSThread::Cancel()
{
	pthread_cancel(thread);
}


void *
MSThread::Join()
{
	void *status = 0;
	if ( 0 != pthread_join(thread,&status) )
		return 0;
	return status;
}
///////////////////////////////////////////////////////

template<class T>
MSProcessingQueue<T>::MSProcessingQueue(unsigned long threadCount, MSThreadFunc threadFunc)
{
	thefunc = threadFunc;
	AddThreads(threadCount);
}

template<class T>
MSProcessingQueue<T>::~MSProcessingQueue()
{
	themutex.Lock();
	while(!threads.empty())
	{
		delete threads.back();
		threads.pop_back();
	}
}

template<class T>
long
MSProcessingQueue<T>::ThreadCount()
{
	themutex.Lock();
	long c = threads.size();
	themutex.Unlock();
	return c;
}

template<class T>
bool
MSProcessingQueue<T>::AddThreads(long count)
{
	themutex.Lock();
	for(long i = 0 ; i < count ; ++i)
	{
		threads.push_back( new MSThread(thefunc,(void*)this) );
	}
	themutex.Unlock();
}

template<class T>
bool
MSProcessingQueue<T>::RemoveThreads(long count)
{
	themutex.Lock();
	for(long i = 0 ; i < count ; ++i)
	{
		delete threads.back();
		threads.pop_back( );
	}
	themutex.Unlock();
}

///////////////////////////////////////////////////////
#endif
