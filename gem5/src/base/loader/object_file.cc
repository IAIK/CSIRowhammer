/*
 * Copyright (c) 2002-2004 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "base/loader/object_file.hh"

#include <string>
#include <vector>

#include "base/loader/raw_image.hh"

namespace Loader
{

ObjectFile::ObjectFile(ImageFileDataPtr ifd) : ImageFile(ifd) {}

const char *
archToString(Arch arch)
{
    switch (arch) {
      case UnknownArch:
        return "unknown";
      case SPARC64:
        return "sparc64";
      case SPARC32:
        return "sparc32";
      case Mips:
        return "mips";
      case X86_64:
        return "x86_64";
      case I386:
        return "i386";
      case Arm64:
        return "arm64";
      case Arm:
        return "arm";
      case Thumb:
        return "thumb";
      case Power:
        return "power";
      case Riscv64:
        return "riscv64";
      case Riscv32:
        return "riscv32";
      default:
        panic("Unrecognized arch %d.", arch);
    }
}

const char *
opSysToString(OpSys op_sys)
{
    switch (op_sys) {
      case UnknownOpSys:
        return "unknown";
      case Tru64:
        return "tru64";
      case Linux:
        return "linux";
      case Solaris:
        return "solaris";
      case LinuxArmOABI:
        return "linux_arm_OABI";
      case FreeBSD:
        return "freebsd";
      default:
        panic("Unrecognized operating system %d.", op_sys);
    }
}

namespace
{

typedef std::vector<ObjectFileFormat *> ObjectFileFormatList;

ObjectFileFormatList &
object_file_formats()
{
    static ObjectFileFormatList formats;
    return formats;
}

} // anonymous namespace

ObjectFileFormat::ObjectFileFormat()
{
    object_file_formats().emplace_back(this);
}

ObjectFile *
createObjectFile(const std::string &fname, bool raw)
{
    ImageFileDataPtr ifd(new ImageFileData(fname));

    for (auto &format: object_file_formats()) {
        ObjectFile *file_obj = format->load(ifd);
        if (file_obj)
            return file_obj;
    }

    if (raw)
        return new RawImage(ifd);

    return nullptr;
}

} // namespace Loader