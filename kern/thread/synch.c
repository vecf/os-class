/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
	struct semaphore *sem;

	sem = kmalloc(sizeof(*sem));
	if (sem == NULL) {
		return NULL;
	}

	sem->sem_name = kstrdup(name);
	if (sem->sem_name == NULL) {
		kfree(sem);
		return NULL;
	}

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
	sem->sem_count = initial_count;

	return sem;
}

void
sem_destroy(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
	kfree(sem->sem_name);
	kfree(sem);
}

void
P(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
	while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
	}
	KASSERT(sem->sem_count > 0);
	sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

	sem->sem_count++;
	KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
	struct lock *lock;

	lock = kmalloc(sizeof(*lock));
	if (lock == NULL) {
		return NULL;
	}

	lock->lk_name = kstrdup(name);
	if (lock->lk_name == NULL) {
		kfree(lock);
		return NULL;
	}

	HANGMAN_LOCKABLEINIT(&lock->lk_hangman, lock->lk_name);

	/*	vecf: 
	// ASST1 - add stuff here as needed
	-initialize spinlock 
	-initialize wchan
	-ensure lock not held and mem is initialized
	*/

	lock->lock_wchan = wchan_create(lock->lk_name);
	if (lock->lock_wchan == NULL) {
		kfree(lock->lk_name);
		kfree(lock);
		return NULL;
	}
	spinlock_init(&lock->lock_lock);

	lock->held = false; //mem properly initialized
	lock->held_by = NULL;

	return lock;
}

void
lock_destroy(struct lock *lock)
{
	/*	vecf: 
	// add stuff here as needed
	-lock must not be held, panic otherwise.
	-clean up spinlock 	(expects `struct spinlock`).
	-clean up wchan 	(expects `struct *wchan`).
				wchan_destroy asserts if anyone is waiting on wchan.
	*/

	KASSERT(lock != NULL);

	KASSERT(lock_do_i_hold(lock)==false); 

	spinlock_cleanup(&lock->lock_lock);
	wchan_destroy(lock->lock_wchan);

	kfree(lock->lk_name);
	kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
	/*	vecf: 
	// ASST1 - Write this
	- get atomic access to lock
	- if already held, sleep until notified
	- if not held, take lock
	- release atomic access
	(basically imitate semaphore without counter)
	TODO avoid duplicate deadlocking calls to acquire
	*/

	KASSERT(lock != NULL);
	KASSERT(curthread->t_in_interrupt == false);

	spinlock_acquire(&lock->lock_lock);

	/* Call this (atomically) before waiting for a lock */
	HANGMAN_WAIT(&curthread->t_hangman, &lock->lk_hangman);

	while (lock->held == true) {
		KASSERT(lock_do_i_hold(lock)==false); // TODO deadlock waiting for self 
		// TODO kprintf("thread %s sleeping.\n",curthread->t_hangman);
		wchan_sleep(lock->lock_wchan, &lock->lock_lock);
	}
	lock->held = true;		//lock not held, take it.
	lock->held_by = curthread; 	//tell who's holding

	/* Call this (atomically) once the lock is acquired */
	HANGMAN_ACQUIRE(&curthread->t_hangman, &lock->lk_hangman);

	spinlock_release(&lock->lock_lock);
}

void
lock_release(struct lock *lock)
{ 
	/*	vecf:
	//ASST1 - write this
	- get atomic access to lock
	- release the lock
	- only holding thread may release: panic otherwise.
	- notify one sleeping thread of realeased lock.
	- release atomic access to the lock
	*/

	KASSERT(lock != NULL);

	spinlock_acquire(&lock->lock_lock);

	KASSERT(lock_do_i_hold(lock) == true);

	lock->held = false;
	lock->held_by = NULL;

	KASSERT(lock->held == false);

	wchan_wakeone(lock->lock_wchan, &lock->lock_lock);

	/* Call this (atomically) when the lock is released */
	HANGMAN_RELEASE(&curthread->t_hangman, &lock->lk_hangman);

	spinlock_release(&lock->lock_lock);
}

bool
lock_do_i_hold(struct lock *lock)
{
	/*	vecf:
	// ASST1 - Write this
	- Since curthread is never NULL, 
	- ... return true if curthread == lock->held_by.
	*/

	KASSERT(curthread != NULL);

	if (curthread == lock->held_by)
		return true;

	return false;
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
	struct cv *cv;

	cv = kmalloc(sizeof(*cv));
	if (cv == NULL) {
		return NULL;
	}

	cv->cv_name = kstrdup(name);
	if (cv->cv_name==NULL) {
		kfree(cv);
		return NULL;
	}

	/*	vecf:
	// add stuff here as needed
	*/

	cv->cv_wchan = wchan_create(cv->cv_name);
	if (cv->cv_wchan == NULL) {
		kfree(cv->cv_name);
		kfree(cv);
		return NULL;
	}
	spinlock_init(&cv->cv_spinlock);

	return cv;
}

void
cv_destroy(struct cv *cv)
{
	KASSERT(cv != NULL);

	/*	vecf:
	// ASST1 - add stuff here as needed
	*/

	spinlock_cleanup(&cv->cv_spinlock);
	wchan_destroy(cv->cv_wchan);
	kfree(cv->cv_name);
	kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	/*	vecf:
	// ASST1 - Write this
	Release supplied lock, go to sleep, and, after 
	waking up again, re-acquire the lock.
	TODO search all ?'s, rename cv_spinlock*/
	spinlock_acquire(&cv->cv_spinlock); //cv_wait must be atomic?

	KASSERT(lock_do_i_hold(lock)==true); //current thread must hold lock

	lock_release(lock);
		//if this isn't atomic?
		// <- here
		//some other thread notifies on this wchan just before we sleep
		//...sleeps forever
	wchan_sleep(cv->cv_wchan, &cv->cv_spinlock);//spinlock released while sleeping
		//...imagine multiple threads woken up at once
		// <- here
		//one wins and gets the lock.
		//winner finishes, calls cv_signal/cv_broadcast again but that channel is
		//empty since everyone woke up
		//everyone is already awake and waiting on the lock
		//if no-one notifies on lock->lock_wchan its a deadlock
	lock_acquire(lock);

	spinlock_release(&cv->cv_spinlock);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	/*	vecf:
	// ASST1 - Write this
	Wake up one thread that's sleeping on this CV.
	*/
	spinlock_acquire(&cv->cv_spinlock); //cv_wait must be atomic?
	KASSERT(lock_do_i_hold(lock)==true); //current thread must hold lock
	wchan_wakeone(cv->cv_wchan, &cv->cv_spinlock);
	spinlock_release(&cv->cv_spinlock);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	/*	vecf:
	// Write this
	Wake up all threads sleeping on this CV.
	*/
	spinlock_acquire(&cv->cv_spinlock); //cv_wait must be atomic?
	KASSERT(lock_do_i_hold(lock)==true); //current thread must hold lock
	wchan_wakeall(cv->cv_wchan, &cv->cv_spinlock);
	spinlock_release(&cv->cv_spinlock);
}
