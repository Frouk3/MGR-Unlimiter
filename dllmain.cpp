#include <Events.h>
#include "Hooks.h"
#include <Hw.h>
#include <common.h>
#include <string>
#include <shared.h>
#include <HwDvd.h>

// Just in case, make every heap having global as reserved

#define DEBUG_HEAP_USAGE 0

#if DEBUG_HEAP_USAGE
LPD3DXFONT font;
bool bCanRender = false;
bool bShowMemoryStatistics = false;

#pragma comment(lib, "d3dx9.lib")
#endif

CREATE_THISCALL(false, shared::base + 0x9D4130, int, Hw_cHeapPhysical_create, Hw::cHeapPhysical*, unsigned int size, Hw::cHeap* heap, const char* target)
{
	auto result = original(pThis, size, heap, target);

	pThis->m_pSubHeap = heap;

	return result;
}

CREATE_THISCALL(false, shared::base + 0x9D39D0, int, Hw_cHeapVariable_create, Hw::cHeapVariable*, unsigned int size, Hw::cHeap* heap, const char* target)
{
	auto result = original(pThis, size * 5, heap, target);

	pThis->m_pSubHeap = heap;

	return result;
}

CREATE_THISCALL(false, shared::base + 0x9D2AB0, int, Hw_cHeapFixed_create, Hw::cHeapFixed*, unsigned int size, unsigned int allocSize, unsigned int preserved, Hw::cHeap* heap, const char* target)
{
	auto result = original(pThis, size, allocSize, preserved, heap, target);

	pThis->m_pSubHeap = heap;

	return result;
}

CREATE_THISCALL(false, shared::base + 0x9D4290, void*, Hw_cHeapPhysicalBaseAllocate, Hw::cHeapPhysicalBase*, size_t size, size_t align, Hw::HW_ALLOC_MODE mode, int a5)
{
	if (mode == Hw::HW_ALLOC_PHYSICAL || mode == Hw::HW_ALLOC_PHYSICAL_BACK)
	{
		if (align != 0x1000)
			Hw::DebugSystem::Report("[cHeapPhysicalBase] Invalid memory acquisition mode (%d,%d)", (size + 3u) & ~3u, align);
	}

	void* heap = pThis->m_pParentHeap->allocImpl(size, align, mode, a5);

	if (!heap)
		Hw::DebugSystem::Report("[cHeapPhysicalBase] Failed to allocate heap[need: %d, available: %d]", (size + 3u) & ~3u, (size + 3u) & ~3u, pThis->m_pParentHeap->getAllocatableSize());

	return heap;
}

#if DEBUG_HEAP_USAGE
tagPOINT& res = *(tagPOINT*)(shared::base + 0x14CE9A4);

void VAddTextA(const Hw::cVec2 &pos, bool bShadow, float shadowOffset, const char *fmt, va_list va)
{
	int size = vsnprintf(nullptr, 0, fmt, va);

	char* buffer = (char*)_alloca(size + 1);

	if (!buffer)
		return;

	vsnprintf(buffer, size + 1, fmt, va);

	RECT rect;

	if (bShadow)
	{
		rect.left = (int)(pos.x + shadowOffset);
		rect.right = 0;
		rect.top = (int)(pos.y + shadowOffset);
		rect.bottom = 0;

		font->DrawTextA(nullptr, buffer, -1, &rect, DT_NOCLIP, 0xFF000000);
	}

	rect.left = (int)pos.x;
	rect.top = (int)pos.y;

	rect.right = 0;
	rect.bottom = 0;

	font->DrawTextA(nullptr, buffer, -1, &rect, DT_NOCLIP, -1);
}

void AddTextA(const Hw::cVec2& pos, bool bShadow, float shadowOffset, const char* fmt, ...)
{
	va_list va;
	va_start(va, fmt);

	VAddTextA(pos, bShadow, shadowOffset, fmt, va);

	va_end(va);
}
#endif

int isPathMatch(
	const char* pCurrentPath,
	const char* pTargetPath,
	const char** ppDirExclude,
	int dirExcludeNum)
{
	unsigned int v8;
	int v9;
	const char* v10;
	signed int v11;
	int v12;
	int v13;
	char v14;
	bool v15;
	char v16;
	char v17;
	const char* a1a;
	const char* a2a;

	if (!pCurrentPath || !ppDirExclude || !dirExcludeNum)
		return 0;

	v8 = strlen(pTargetPath);
	a2a = (const char*)strlen(pCurrentPath);
	v9 = 0;
	a1a = (const char*)(v8 - 1);

	while (1)
	{
		v10 = ppDirExclude[v9];
		v11 = strlen(v10) - 1;
		v12 = (int)a1a;

		if ((int)a1a <= v11)
		{
			if ((int)a1a < 0)
				break;
			while (v10[v11] == pTargetPath[v12])
			{
				--v11;
				if (--v12 < 0)
					goto LABEL_12;
			}
			if (v12 < 0)
				break;
		}
	LABEL_26:
		if (++v9 >= (unsigned int)dirExcludeNum)
			return 0;
	}

LABEL_12:
	v13 = (int)(a2a - 1);
	if (v11 > (int)(a2a - 1))
		goto LABEL_26;

	if (v11 >= 0)
	{
		while (1)
		{
			v14 = v10[v11];
			if (v14 != '\\' && v14 != '/')
				break;
			v16 = pCurrentPath[v13];
			if (v16 != '\\')
			{
				v15 = v16 == '/';
			LABEL_19:
				if (!v15)
					goto LABEL_26;
			}
			--v13;
			if (--v11 < 0)
				goto LABEL_23;
		}
		v15 = v14 == pCurrentPath[v13];
		goto LABEL_19;
	}

LABEL_23:
	if (v13 < 0)
		return 1;

	v17 = pCurrentPath[v13];
	if (v17 != '\\' && v17 != '/')
		goto LABEL_26;

	return 1;
}

CREATE_THISCALL(false, shared::base + 0x9EAB30, int, Hw_cDvdFst_startup, Hw::cDvdFst*, const char* pRootFilePath, const char* pPatchFilePath, Hw::cHeap& rHeap, const char** ppDirExclude, int dirExcludeNum)
{
	size_t fileAmount = 0;
	size_t dirAmount = 0;
	size_t nameBufferSz = 0;

	auto processFd = [&](const char* path, const char* dirName, auto&& processFd) -> bool
	{
		char* pathBuffer = new char[260]; // it's better to operate on RAM rather than stack, because of the recursion

		if (dirName)
		{
			strcpy_s(pathBuffer, 260, path);
			if (pathBuffer[strlen(pathBuffer) - 1] != '\\')
				strcat_s(pathBuffer, 260, "\\");

			strcat_s(pathBuffer, 260, dirName);
			if (pathBuffer[strlen(pathBuffer) - 1] != '\\')
				strcat_s(pathBuffer, 260, "\\");
		}
		else
		{
			strcpy_s(pathBuffer, 260, path);
		}

		Hw::cDvdFileFind fd;
		if (!fd.startup(pathBuffer))
		{
			delete[] pathBuffer;
			return false;
		}

		for (; fd.isValid(); fd.setNext())
		{
			if (fd.isName(".") || fd.isName("..") || fd.isName(".svn"))
				continue;

			if (fd.isDirectory())
			{
				if (!isPathMatch(pathBuffer, fd.refName(), ppDirExclude, dirExcludeNum))
				{
					dirAmount++;
					processFd(pathBuffer, fd.refName(), processFd);

					char* newPathBuffer = new char[260];
					strcpy_s(newPathBuffer, 260, fd.refName());
					if (newPathBuffer[strlen(newPathBuffer) - 1] != '\\')
						strcat_s(newPathBuffer, 260, "\\");

					nameBufferSz += strlen(newPathBuffer) + 1;
					delete[] newPathBuffer;
				}
			}
			else if (fd.isFile() && fd.getSize()) // ignore empty files, same as game does
			{
				fileAmount++;
				nameBufferSz += strlen(fd.refName()) + 1;
			}
		}

		fd.cleanup();

		delete[] pathBuffer;

		return true;
	};

	nameBufferSz += strlen(pRootFilePath) + 1;

	if (processFd(pRootFilePath, "", processFd))
	{
		pThis->m_RootBuffer.m_DirBufferSize = dirAmount;
		pThis->m_RootBuffer.m_FileBufferSize = fileAmount;
		pThis->m_RootBuffer.m_NameBufferSize = nameBufferSz;
	}

	if (pPatchFilePath)
	{
		fileAmount = 0;
		dirAmount = 0;
		nameBufferSz = 0;

		nameBufferSz += strlen(pPatchFilePath) + 1;

		if (processFd(pPatchFilePath, "", processFd))
		{
			pThis->m_PatchBuffer.m_DirBufferSize = dirAmount;
			pThis->m_PatchBuffer.m_FileBufferSize = fileAmount;
			pThis->m_PatchBuffer.m_NameBufferSize = nameBufferSz;
		}
	}

	return original(pThis, pRootFilePath, pPatchFilePath, rHeap, ppDirExclude, dirExcludeNum);
}

class Plugin
{
public:
	Plugin()
	{
		// injector::MakeNOP(shared::base + 0x9D437E, 3, true); // m_nTotalSize -= size;
		// injector::MakeNOP(shared::base + 0x9D4393, 3, true); // m_nTotalSize -= size;
		// injector::MakeNOP(shared::base + 0x9D43AD, 3, true); // m_nFreeMemory -= size;
		// injector::MakeNOP(shared::base + 0x9D3914, 2, true); // if (blockCapacity > 255) blockCapacity = 255;
		// injector::MakeNOP(shared::base + 0x9D46C4, 3, true); // m_nFreeMemory -= size;

		injector::WriteMemory<unsigned char>(shared::base + 0x9D3C6B, 0xEB, true); // jge -> jmp
		injector::MakeNOP(shared::base + 0x9D3C8E, 3, true); // sub [esi+50h], edi

		injector::WriteMemory<int>(shared::base + 0x61E20D + 1, 0xFFFFFFFF, true); // GET OUT--

#if DEBUG_HEAP_USAGE
		Events::OnApplicationStartEvent.after += []()
			{
				if (SUCCEEDED(D3DXCreateFontA(Hw::GraphicDevice::m_pDevice, 17, 0, FW_BOLD, 0, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, DEFAULT_QUALITY, FF_DONTCARE | DEFAULT_PITCH, "Segoe UI", &font)))
					bCanRender = true;
			};

		Events::OnEndScene += []()
			{
				if (!bCanRender)
					return;

				if (g_Keyboard.trig(Hw::KB_F5))
					bShowMemoryStatistics ^= true;

				if (bShowMemoryStatistics)
				{
					auto getProperSize = [](size_t size) -> std::string
						{
							char buffer[128];

							const float KB = 1024.f;
							const float MB = KB * KB;
							const float GB = KB * MB;

							const char* format = "";

							if (size >= GB)
							{
								format = "%.2f GB";
								sprintf(buffer, format, size / GB);
							}
							else if (size >= MB)
							{
								format = "%.2f MB";
								sprintf(buffer, format, size / MB);
							}
							else if (size >= KB)
							{
								format = "%.2f KB";
								sprintf(buffer, format, size / KB);
							}
							else
							{
								format = "%.2f Bytes";
								sprintf(buffer, format, static_cast<float>(size));
							}

							return buffer;
						};

					size_t overallUsed = 0;
					size_t sizeLimit = 0;
					size_t allocatable = 0;

					float textOffset = 30.f;

					Hw::cHeapGlobal* gHeap = Hw::cHeapGlobal::GetInstance();

					if (!gHeap)
						return;

					for (Hw::cHeap* heap = gHeap->m_pPrev; heap; heap = heap->m_pNext)
					{
						if (heap == gHeap)
							continue;

						overallUsed += heap->getUsedSize();
						sizeLimit += heap->getSize();
						allocatable += heap->getAllocatableSize();

						AddTextA({ 15.f, textOffset }, true, 1.f, "%s [%s/%s] <- %s", heap->m_pHeapName, getProperSize(heap->getUsedSize()).c_str(), getProperSize(heap->getSize()).c_str(), heap->m_pParentHeap->m_pHeapName);

						textOffset += 15.f;
					}

					AddTextA({ 15.f, 15.f }, true, 1.f, "%s: [%s/%s]", gHeap->m_pHeapName, getProperSize(gHeap->getUsedSize()).c_str(), getProperSize(gHeap->getAllocatableSize()).c_str());

					AddTextA({ 15.f, textOffset }, true, 1.f, "TOTAL: [%s/%s] -> %.2f%% out of %s", getProperSize(allocatable + gHeap->getAllocatableSize()).c_str(), getProperSize(sizeLimit + gHeap->getSize()).c_str(), float(allocatable + gHeap->getAllocatableSize()) / gHeap->getSize() * 100.f, getProperSize(gHeap->getSize()).c_str());
				}
			};

		Events::OnDeviceReset.before += []()
			{
				font->OnLostDevice();
			};

		Events::OnDeviceReset.after += []()
			{
				font->OnResetDevice();
			};
#endif
	}
} plugin;