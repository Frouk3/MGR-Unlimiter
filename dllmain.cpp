#include <Events.h>
#include "Hooks.h"
#include <Hw.h>
#include <common.h>
#include <string>
#include <shared.h>

// Just in case, make every heap having global as reserved

#define DEBUG_HEAP_USAGE 1

#if DEBUG_HEAP_USAGE
LPD3DXFONT font;
bool bCanRender = false;
bool bShowMemoryStatistics = false;

#pragma comment(lib, "d3dx9.lib")
#endif

CREATE_THISCALL(false, shared::base + 0x9D4130, int, Hw_cHeapPhysical_create, Hw::cHeapPhysical*, unsigned int size, Hw::cHeap* heap, const char* target)
{
	auto result = oHw_cHeapPhysical_create(pThis, size, heap, target);

	pThis->m_pReservedHeap = heap;

	return result;
}

CREATE_THISCALL(false, shared::base + 0x9D39D0, int, Hw_cHeapVariable_create, Hw::cHeapVariable*, unsigned int size, Hw::cHeap* heap, const char* target)
{
	auto result = oHw_cHeapVariable_create(pThis, size * 5, heap, target);

	pThis->m_pReservedHeap = heap;

	return result;
}

CREATE_THISCALL(false, shared::base + 0x9D2AB0, int, Hw_cHeapFixed_create, Hw::cHeapFixed*, unsigned int size, unsigned int allocSize, unsigned int preserved, Hw::cHeap* heap, const char* target)
{
	auto result = oHw_cHeapFixed_create(pThis, size, allocSize, preserved, heap, target);

	pThis->m_pReservedHeap = heap;

	return result;
}

CREATE_THISCALL(false, shared::base + 0x9D4290, void*, Hw_cHeapPhysicalBaseAllocate, Hw::cHeapPhysicalBase*, size_t size, size_t preserved, int alignment, int a5)
{
	size_t alignedReserved = (preserved + 3u) & ~3u;
	size_t alignedSize = (size + 3u) & ~3u;

	if (alignment == 1 || alignment == 2)
	{
		if (alignedReserved != 0x1000)
			PrintfLog("[cHeapPhysicalBase] Invalid memory acquisition mode (%d,%d)", alignedReserved, alignment);
	}

	void* heap = pThis->m_pHeapOwner->allocate(size, preserved, alignment, a5);

	if (!heap)
		PrintfLog("[cHeapPhysicalBase] Failed to allocate heap[need: %d, available: %d]", alignedSize, alignedSize - pThis->m_pHeapOwner->getFreeMemory());

	return heap;
}

#if DEBUG_HEAP_USAGE
tagPOINT& res = *(tagPOINT*)(shared::base + 0x14CE9A4);

void VAddTextA(const cVec2 &pos, bool bShadow, float shadowOffset, const char *fmt, va_list va)
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

void AddTextA(const cVec2& pos, bool bShadow, float shadowOffset, const char* fmt, ...)
{
	va_list va;
	va_start(va, fmt);

	VAddTextA(pos, bShadow, shadowOffset, fmt, va);

	va_end(va);
}
#endif

class Plugin
{
public:
	Plugin()
	{
		MH_Initialize();

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
				if (SUCCEEDED(D3DXCreateFontA(Hw::GraphicDevice, 17, 0, FW_BOLD, 0, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, DEFAULT_QUALITY, FF_DONTCARE | DEFAULT_PITCH, "Segoe UI", &font)))
					bCanRender = true;
			};

		Events::OnEndScene += []()
			{
				if (!bCanRender)
					return;

				if (shared::IsKeyPressed(VK_F5, false))
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
					size_t freeMemory = 0;

					float textOffset = 30.f;

					Hw::cHeapGlobal* gHeap = Hw::cHeapGlobal::GetInstance();

					if (!gHeap)
						return;

					for (Hw::cHeap* heap = gHeap->m_pPrev; heap; heap = heap->m_pNext)
					{
						overallUsed += heap->getUsedMemory();
						sizeLimit += heap->getMemoryLimit();
						freeMemory += heap->getFreeMemory();

						AddTextA({ 15.f, textOffset }, true, 1.f, "%s [%s/%s] <- %s", heap->m_TargetAlloc, getProperSize(heap->getUsedMemory()).c_str(), getProperSize(heap->getMemoryLimit()).c_str(), heap->m_pHeapOwner->m_TargetAlloc);

						textOffset += 15.f;
					}

					AddTextA({ 15.f, 15.f }, true, 1.f, "%s: [%s/%s]", gHeap->m_TargetAlloc, getProperSize(gHeap->getUsedMemory()).c_str(), getProperSize(gHeap->getMemoryLimit()).c_str());

					AddTextA({ 15.f, textOffset }, true, 1.f, "TOTAL: [%s/%s] -> %.2f%% out of %s", getProperSize(overallUsed + gHeap->getUsedMemory()).c_str(), getProperSize(sizeLimit).c_str(), (((float)overallUsed + (float)gHeap->getUsedMemory()) / (float)gHeap->getMemoryLimit()) * 100.f, getProperSize(gHeap->getMemoryLimit()).c_str());
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