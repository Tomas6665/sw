/*
 * Copyright (c) 2016, Egor Pugin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     1. Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *     2. Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *     3. Neither the name of the copyright holder nor the names of
 *        its contributors may be used to endorse or promote products
 *        derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "dependency.h"

#include "config.h"

#include <iostream>

Package extractFromString(const String &target)
{
    ProjectPath p = target.substr(0, target.find('-'));
    Version v = target.substr(target.find('-') + 1);
    return{ p,v };
}

path Package::getDirSrc() const
{
    return directories.storage_dir_src / getHashPath();
}

path Package::getDirObj() const
{
    return directories.storage_dir_obj / getHashPath();
}

path Package::getStampFilename() const
{
    auto b = directories.storage_dir_etc / STAMPS_DIR / "packages" / getHashPath();
    auto f = b.filename();
    b = b.parent_path();
    b /= get_stamp_filename(f.string());
    return b;
}

String Package::getHash() const
{
    static const auto delim = "/";
    return sha1(ppath.toString() + delim + version.toString()).substr(0, 8);
}

path Package::getHashPath() const
{
    auto h = getHash();
    path p;
    p /= h.substr(0, 2);
    p /= h.substr(2, 2);
    p /= h.substr(4);
    return p;
}

void Package::createNames()
{
    auto v = version.toAnyVersion();
    target_name = ppath.toString() + (v == "*" ? "" : ("-" + v));
    variable_name = ppath.toString() + "_" + (v == "*" ? "" : ("_" + v));
    std::replace(variable_name.begin(), variable_name.end(), '.', '_');
}

String Package::getTargetName() const
{
    if (target_name.empty())
    {
        auto v = version.toAnyVersion();
        return ppath.toString() + (v == "*" ? "" : ("-" + v));
    }
    return target_name;
}

String Package::getVariableName() const
{
    if (variable_name.empty())
    {
        auto v = version.toAnyVersion();
        auto vname = ppath.toString() + "_" + (v == "*" ? "" : ("_" + v));
        std::replace(vname.begin(), vname.end(), '.', '_');
        return vname;
    }
    return variable_name;
}
