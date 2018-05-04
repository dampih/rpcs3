#pragma once

#include "Utilities/File.h"
#include "SPUThread.h"
#include <vector>
#include <string>
#include <bitset>
#include <memory>

// Helper class
class spu_cache
{
	fs::file m_file;

public:
	spu_cache(const std::string& loc);

	~spu_cache();

	operator bool() const
	{
		return m_file.operator bool();
	}

	std::vector<std::vector<u32>> get();

	void add(const std::vector<u32>& func);

	static void initialize(const std::string& ppu_cache);
};

// SPU Recompiler instance base class
class spu_recompiler_base
{
protected:
	u32 m_pos;

	std::bitset<0x10000> m_block_info;

	std::shared_ptr<spu_cache> m_cache;

public:
	spu_recompiler_base();

	virtual ~spu_recompiler_base();

	// Get pointer to the trampoline at given position
	virtual spu_function_t get(u32 lsa) = 0;

	// Compile function
	virtual spu_function_t compile(std::vector<u32>&&) = 0;

	// Default dispatch function fallback (second arg is unused)
	static void dispatch(SPUThread&, void*, u8* rip);

	// Target for the unresolved patch point (second arg is unused)
	static void branch(SPUThread&, void*, u8* rip);

	// Get the block at specified address
	static std::vector<u32> block(SPUThread&, u32 lsa, std::bitset<0x10000>&);

	// Create recompiler instance (ASMJIT)
	static std::unique_ptr<spu_recompiler_base> make_asmjit_recompiler();

	// Create recompiler instance (LLVM)
	static std::unique_ptr<spu_recompiler_base> make_llvm_recompiler();
};
