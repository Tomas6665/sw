// Copyright (C) 2017-2019 Egor Pugin <egor.pugin@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "build_settings.h"
#include "checks_storage.h"
#include "command.h"
#include "target/base.h"

#include <sw/builder/file_storage.h>
#include <sw/core/build.h>
#include <sw/core/target.h>
#include <sw/manager/package_data.h>

#include <any>
#include <variant>

namespace sw
{

struct Build;
namespace driver::cpp { struct Driver; }
struct Module;
struct ModuleStorage;
struct SwContext;
struct ModuleSwappableData;

using FilesMap = std::unordered_map<path, path>;

struct SW_DRIVER_CPP_API Test : driver::CommandBuilder
{
    using driver::CommandBuilder::CommandBuilder;

    Test() = default;
    Test(const driver::CommandBuilder &cb)
        : driver::CommandBuilder(cb)
    {}

    void prepare(const Build &s)
    {
        // todo?
    }
};

struct SW_DRIVER_CPP_API SimpleBuild : TargetBase
{
    // public functions for sw frontend
};

struct SW_DRIVER_CPP_API Build : SimpleBuild
{
    using Base = SimpleBuild;

    ModuleSwappableData *module_data = nullptr;
    SourceDirMap source_dirs_by_source;
    std::unordered_map<PackageId, path> source_dirs_by_package;
    Checker checker;

    SwContext &getContext() const;
    const OS &getHostOs() const;
    const TargetSettings &getSettings() const;
    bool isKnownTarget(const LocalPackage &p) const;
    path getSourceDir(const LocalPackage &p) const;
    std::optional<path> getSourceDir(const Source &s, const Version &v) const;
    bool skipTarget(TargetScope Scope) const;
    TargetMap &getChildren();
    const TargetMap &getChildren() const;
    path getChecksDir() const;
    void setModuleData(ModuleSwappableData &);
    ModuleSwappableData &getModuleData() const;

    // tests
    // TODO: implement some of https://cmake.org/cmake/help/latest/manual/cmake-properties.7.html#properties-on-tests
    Commands tests;
    Test addTest(const ExecutableTarget &t);
    Test addTest(const String &name, const ExecutableTarget &t);
    Test addTest();
    Test addTest(const String &name);
    path getTestDir() const;

private:
    void addTest(Test &cb, const String &name);

    //
public:
    Build(SwBuild &);

    Module loadModule(const path &fn) const;

    // move to some other place?
    void cppan_load(yaml &root, const String &root_name = {});

private:
    void cppan_load1(const yaml &root, const String &root_name);
};

}
