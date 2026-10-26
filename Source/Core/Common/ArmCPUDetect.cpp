// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#else
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif

#include "Common/CommonTypes.h"
#include "Common/CPUDetect.h"
#include "Common/StringUtil.h"

#if defined(__APPLE__)
static bool AppleArmFeature(const char* name)
{
	// Never claim a feature the sysctl does not confirm; every Apple Silicon
	// chip implements these, so a confirmed result is the normal path.
	int64_t value = 0;
	size_t size = sizeof(value);
	return sysctlbyname(name, &value, &size, nullptr, 0) == 0 && value != 0;
}

static std::string GetCPUString()
{
	char brand[128] = "Unknown";
	size_t size = sizeof(brand);
	sysctlbyname("machdep.cpu.brand_string", brand, &size, nullptr, 0);
	return brand;
}
#else
const char procfile[] = "/proc/cpuinfo";

static std::string GetCPUString()
{
	const std::string marker = "Hardware\t: ";
	std::string cpu_string = "Unknown";

	std::string line;
	std::ifstream file(procfile);

	if (!file)
		return cpu_string;

	while (std::getline(file, line))
	{
		if (line.find(marker) != std::string::npos)
		{
			cpu_string = line.substr(marker.length());
			break;
		}
	}

	return cpu_string;
}
#endif

CPUInfo cpu_info;

CPUInfo::CPUInfo()
{
	Detect();
}

// Detects the various CPU features
void CPUInfo::Detect()
{
	// Set some defaults here
	// When ARMv8 CPUs come out, these need to be updated.
	HTT = false;
	OS64bit = true;
	CPU64bit = true;
	Mode64bit = true;
	vendor = VENDOR_ARM;

	// Get the information about the CPU
	num_cores = sysconf(_SC_NPROCESSORS_CONF);
	strncpy(cpu_string, GetCPUString().c_str(), sizeof(cpu_string));

#if defined(__APPLE__)
	// Every Apple Silicon chip implements these; confirm where the kernel
	// exposes them so nothing is claimed without support.
	bFP = true;
	bASIMD = true;
	bAES = AppleArmFeature("hw.optional.arm.FEAT_AES");
	bCRC32 = AppleArmFeature("hw.optional.arm.FEAT_CRC32");
	bSHA1 = AppleArmFeature("hw.optional.arm.FEAT_SHA1");
	bSHA2 = AppleArmFeature("hw.optional.arm.FEAT_SHA256");
#else
	unsigned long hwcaps = getauxval(AT_HWCAP);
	bFP = hwcaps & HWCAP_FP;
	bASIMD = hwcaps & HWCAP_ASIMD;
	bAES = hwcaps & HWCAP_AES;
	bCRC32 = hwcaps & HWCAP_CRC32;
	bSHA1 = hwcaps & HWCAP_SHA1;
	bSHA2 = hwcaps & HWCAP_SHA2;
#endif
}

// Turn the CPU info into a string we can show
std::string CPUInfo::Summarize()
{
	std::string sum;
	if (num_cores == 1)
		sum = StringFromFormat("%s, %i core", cpu_string, num_cores);
	else
		sum = StringFromFormat("%s, %i cores", cpu_string, num_cores);

	if (bAES) sum += ", AES";
	if (bCRC32) sum += ", CRC32";
	if (bSHA1) sum += ", SHA1";
	if (bSHA2) sum += ", SHA2";
	if (CPU64bit) sum += ", 64-bit";

	return sum;
}
