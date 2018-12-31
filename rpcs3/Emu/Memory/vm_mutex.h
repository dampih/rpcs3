#pragma once

#include <mutex>
#include "Utilities/types.h"
#include "Utilities/Atomic.h"

// Shared mutex with conditional address tests for waiters
class vm_mutex final
{
	enum : u32
	{
		c_one = 1u << 14, // Fixed-point 1.0 value (one writer, max_readers = c_one - 1)
		c_sig = 1u << 30,
		c_err = 1u << 31,
	};

	// [63..32] address
	// [31..0] State 
	atomic_t<u64> m_value{};

	void imp_lock_shared();
	void imp_unlock_shared(u32 old);
	void imp_wait(const u32 addr);
	void imp_signal();
	void imp_lock(const u32 addr);
	void imp_unlock(u32 old);
	void imp_lock_upgrade(const u32 addr);
	void imp_lock_unlock();

public:
	constexpr vm_mutex() = default;

	bool try_lock_shared()
	{
		const u64 value = m_value.load();

		// Conditional increment
		return value < c_one - 1 && m_value.compare_and_swap_test(value, value + 1);
	}

	void lock_shared()
	{
		const u64 value = m_value.load();

		if (UNLIKELY(value >= c_one - 1 || !m_value.compare_and_swap_test(value, value + 1)))
		{
			verify("vm_mutex underflow" HERE), (u32)value < c_err;
			imp_lock_shared();
		}
	}

	void unlock_shared()
	{
		// Unconditional decrement (can result in broken state)
		const u32 value = (u32)m_value.fetch_sub(1);

		if (UNLIKELY(value >= c_one))
		{
			imp_unlock_shared(value);
		}
	}

	bool try_lock(const u32 addr)
	{
		return m_value.compare_and_swap_test(0, ((u64)addr << 32) | c_one);
	}

	void lock(const u32 addr)
	{
		if (UNLIKELY(!try_lock(addr)))
		{
			imp_lock(addr);
		}
	}

	void unlock()
	{
		// Unconditional decrement (can result in broken state)
		const u32 value = m_value.fetch_op([](u64& val)
		{
			val -= c_one;

			// Clear address
			val &= UINT32_MAX;
		});

		if (UNLIKELY(value != c_one))
		{
			imp_unlock(value);
		}
	}

	bool try_lock_upgrade(const u32 addr)
	{
		const u64 value = m_value.load();

		// Conditional increment, try to convert a single reader into a writer, ignoring other writers
		return (u32(value) + c_one - 1) % c_one == 0 && m_value.compare_and_swap_test(value, ((u64)addr << 32) | (value + c_one - 1));
	}

	void lock_upgrade(const u32 addr)
	{
		if (UNLIKELY(!try_lock_upgrade(addr)))
		{
			imp_lock_upgrade(addr);
		}
	}

	void lock_downgrade()
	{
		// Convert to reader lock (can result in broken state)
		m_value.atomic_op([&](u64& val)
		{
			val -= c_one - 1;

			// Clear address
			val &= UINT32_MAX;
		});
	}

	// Optimized wait for lockability without locking, relaxed
	void lock_unlock()
	{
		if (UNLIKELY(m_value != 0))
		{
			imp_lock_unlock();
		}
	}

	// Check whether can immediately obtain an exclusive (writer) lock
	bool is_free() const
	{
		return m_value.load() == 0;
	}

	// Check whether can immediately obtain a shared (reader) lock
	bool is_lockable() const
	{
		return m_value.load() < c_one - 1;
	}

	// Check whether can immediatly obtain a shared (reader) lock based address
	bool is_lockable(u32 addr, u32 size)
	{
		// Get address
		const u32 target = *(reinterpret_cast<u32*>(&m_value.raw()) + 1);

		if (addr > target)
		{
			return true;
		}

		if (addr + size <= target)
		{
			return true;
		}

		return false;
	}
};
