#include "stdafx.h"
#include "Utilities/sync.h"
#include "vm_mutex.h"

#include <climits>

void vm_mutex::imp_lock_shared()
{
	for (int i = 0; i < 10; i++)
	{
		busy_wait();

		if (try_lock_shared())
		{
			return;
		}
	}

	// Acquire writer lock and downgrade
	const u32 old = m_value.fetch_add(c_one);

	if (old == 0)
	{
		lock_downgrade();
		return;
	}

	verify("vm_mutex overflow" HERE), (old % c_sig) + c_one < c_sig;
	imp_wait(0);
	lock_downgrade();
}

void vm_mutex::imp_unlock_shared(u32 old)
{
	verify("vm_mutex underflow" HERE), old - 1 < c_err;

	// Check reader count, notify the writer if necessary
	if ((old - 1) % c_one == 0)
	{
		imp_signal();
	}
}

void vm_mutex::imp_wait(const u32 addr)
{
	while (!balanced_wait_until(m_value, -1, [&](u64& value, auto...)
	{
		if ((u32)value >= c_sig)
		{
			value -= c_sig;
			value &= UINT32_MAX;
			value |= (u32)addr << 32;
			return true;
		}

		return false;
	}))
	{
	}
}

void vm_mutex::imp_signal()
{
	m_value += c_sig;
	balanced_awaken(m_value, 1);
}

void vm_mutex::imp_lock(const u32 addr)
{
	for (int i = 0; i < 10; i++)
	{
		busy_wait();

		if (!m_value && try_lock(addr))
		{
			return;
		}
	}

	const u32 old = m_value.fetch_add(c_one);

	if (old == 0)
	{
		return;
	}

	verify("vm_mutex overflow" HERE), (old % c_sig) + c_one < c_sig;
	imp_wait(addr);
}

void vm_mutex::imp_unlock(u32 old)
{
	verify("vm_mutex underflow" HERE), old - c_one < c_err;

	// 1) Notify the next writer if necessary
	// 2) Notify all readers otherwise if necessary (currently indistinguishable from writers)
	if (old - c_one)
	{
		imp_signal();
	}
}

void vm_mutex::imp_lock_upgrade(const u32 addr)
{
	for (int i = 0; i < 10; i++)
	{
		busy_wait();

		if (try_lock_upgrade(addr))
		{
			return;
		}
	}

	// Convert to writer lock
	const u32 old = m_value.fetch_add(c_one - 1);

	verify("vm_mutex overflow" HERE), (old % c_sig) + c_one - 1 < c_sig;

	if (old % c_one == 1)
	{
		return;
	}

	imp_wait(addr);
}

void vm_mutex::imp_lock_unlock()
{
	u32 _max = 1;

	for (int i = 0; i < 30; i++)
	{
		const u32 val = (u32)m_value;

		if (val % c_one == 0 && (val / c_one < _max || val >= c_sig))
		{
			// Return if have cought a state where:
			// 1) Mutex is free
			// 2) Total number of waiters decreased since last check
			// 3) Signal bit is set (if used on the platform)
			return;
		}

		_max = val / c_one;

		busy_wait(1500);
	}

	// Lock and unlock
	if (!m_value.fetch_add(c_one))
	{
		unlock();
		return;
	}

	imp_wait(0);
	unlock();
}
