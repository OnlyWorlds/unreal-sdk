#include "OWConvert.h"

#include <type_traits>

namespace OWConvert
{
	std::string Utf8(const FString& S)
	{
		using FUnit = std::make_unsigned_t<TCHAR>;
		const TCHAR* At = *S;
		const int32 Count = S.Len();
		std::string Out;
		Out.reserve(static_cast<size_t>(Count));
		for (int32 I = 0; I < Count; ++I)
		{
			uint32 Cp = static_cast<FUnit>(At[I]);
			if constexpr (sizeof(TCHAR) == 2)
			{
				if (Cp >= 0xD800 && Cp <= 0xDBFF && I + 1 < Count)
				{
					const uint32 Low = static_cast<FUnit>(At[I + 1]);
					if (Low >= 0xDC00 && Low <= 0xDFFF)
					{
						Cp = 0x10000 + ((Cp - 0xD800) << 10) + (Low - 0xDC00);
						++I;
					}
				}
			}
			else if (Cp > 0x10FFFF)
			{
				Cp = 0xFFFD;
			}
			if (Cp < 0x80)
			{
				Out.push_back(static_cast<char>(Cp));
			}
			else if (Cp < 0x800)
			{
				Out.push_back(static_cast<char>(0xC0 | (Cp >> 6)));
				Out.push_back(static_cast<char>(0x80 | (Cp & 0x3F)));
			}
			else if (Cp < 0x10000)
			{
				Out.push_back(static_cast<char>(0xE0 | (Cp >> 12)));
				Out.push_back(static_cast<char>(0x80 | ((Cp >> 6) & 0x3F)));
				Out.push_back(static_cast<char>(0x80 | (Cp & 0x3F)));
			}
			else
			{
				Out.push_back(static_cast<char>(0xF0 | (Cp >> 18)));
				Out.push_back(static_cast<char>(0x80 | ((Cp >> 12) & 0x3F)));
				Out.push_back(static_cast<char>(0x80 | ((Cp >> 6) & 0x3F)));
				Out.push_back(static_cast<char>(0x80 | (Cp & 0x3F)));
			}
		}
		return Out;
	}

	FString Str(const std::string& S)
	{
		const unsigned char* Bytes = reinterpret_cast<const unsigned char*>(S.data());
		const size_t Size = S.size();
		FString Out;
		if (Size == 0) return Out;
		auto& Units = Out.GetCharArray();
		Units.Reserve(static_cast<int32>(Size) + 1);
		size_t I = 0;
		while (I < Size)
		{
			const uint32 Lead = Bytes[I];
			uint32 Cp = Lead;
			size_t Need = 0;
			uint32 Lo = 0x80;
			uint32 Hi = 0xBF;
			if (Lead < 0x80) {}
			else if (Lead >= 0xC2 && Lead <= 0xDF) { Need = 1; Cp = Lead & 0x1F; }
			else if (Lead >= 0xE0 && Lead <= 0xEF) { Need = 2; Cp = Lead & 0x0F; if (Lead == 0xE0) Lo = 0xA0; }
			else if (Lead >= 0xF0 && Lead <= 0xF4) { Need = 3; Cp = Lead & 0x07; if (Lead == 0xF0) Lo = 0x90; if (Lead == 0xF4) Hi = 0x8F; }
			else { Cp = 0xFFFD; }
			// ED's second byte runs to BF, not 9F: ED A0..BF is a surrogate, which WTF-8 keeps.
			size_t Next = I + 1;
			for (size_t K = 0; K < Need; ++K, ++Next)
			{
				const uint32 B = Next < Size ? Bytes[Next] : 0;
				if (Next >= Size || B < Lo || B > Hi)
				{
					Cp = 0xFFFD;  // the maximal subpart read so far is one U+FFFD; the byte that broke it is read again
					break;
				}
				Lo = 0x80;
				Hi = 0xBF;
				Cp = (Cp << 6) | (B & 0x3F);
			}
			I = Next;
			if constexpr (sizeof(TCHAR) == 2)
			{
				if (Cp >= 0x10000)
				{
					Units.Add(static_cast<TCHAR>(0xD800 + ((Cp - 0x10000) >> 10)));
					Units.Add(static_cast<TCHAR>(0xDC00 + ((Cp - 0x10000) & 0x3FF)));
					continue;
				}
			}
			Units.Add(static_cast<TCHAR>(Cp));
		}
		Units.Add(TCHAR(0));
		return Out;
	}
}
