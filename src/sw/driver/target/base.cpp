// Copyright (C) 2017-2018 Egor Pugin <egor.pugin@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "base.h"

#include "sw/driver/command.h"
#include "sw/driver/jumppad.h"
#include "sw/driver/suffix.h"
#include "sw/driver/solution.h"

#include <sw/builder/sw_context.h>
#include <sw/manager/database.h>
#include <sw/manager/package_data.h>
#include <sw/manager/storage.h>
#include <sw/support/hash.h>

#include <primitives/log.h>
DECLARE_STATIC_LOGGER(logger, "target");

#define SW_BDIR_NAME "bd" // build (binary) dir
#define SW_BDIR_PRIVATE_NAME "bdp" // build (binary) private dir

namespace sw
{

struct TargetSettings
{
    SolutionSettings ss;
    // features (options)
    // deps

    bool operator<(const TargetSettings &) const;
};

bool isExecutable(TargetType t)
{
    return
        0
        || t == TargetType::NativeExecutable
        || t == TargetType::CSharpExecutable
        || t == TargetType::RustExecutable
        || t == TargetType::GoExecutable
        || t == TargetType::FortranExecutable
        || t == TargetType::JavaExecutable
        || t == TargetType::KotlinExecutable
        || t == TargetType::DExecutable
        ;
}

String toString(TargetType T)
{
    switch (T)
    {
#define CASE(x) \
    case TargetType::x: \
        return #x

        CASE(Build);
        CASE(Solution);
        CASE(Project);
        CASE(Directory);
        CASE(NativeLibrary);
        CASE(NativeExecutable);

#undef CASE
    }
    throw SW_RUNTIME_ERROR("unreachable code");
}

TargetBase::TargetBase()
{
}

TargetBase::TargetBase(const TargetBase &rhs)
    : LanguageStorage(rhs)
    , ProjectDirectories(rhs)
    //, pkg(std::make_unique<LocalPackage>(static_cast<const LocalStorage &>(rhs.pkg->storage), rhs.pkg->ppath, rhs.pkg->version))
    , source(rhs.source)
    , Scope(rhs.Scope)
    , Local(rhs.Local)
    , UseStorageBinaryDir(rhs.UseStorageBinaryDir)
    , PostponeFileResolving(rhs.PostponeFileResolving)
    , DryRun(rhs.DryRun)
    , NamePrefix(rhs.NamePrefix)
    , solution(rhs.solution)
    , RootDirectory(rhs.RootDirectory)
{
}

TargetBase::~TargetBase()
{
}

bool TargetBase::hasSameParent(const TargetBase *t) const
{
    if (this == t)
        return true;
    return getPackage().ppath.hasSameParent(t->getPackage().ppath);
}

path TargetBase::getObjectDir() const
{
    return getObjectDir(getPackage(), getConfig(true));
}

path TargetBase::getObjectDir(const LocalPackage &in) const
{
    return getObjectDir(in, getConfig(true));
}

path TargetBase::getObjectDir(const LocalPackage &pkg, const String &cfg)
{
    // bld was build
    return pkg.getDirObj() / "bld" / cfg;
}

TargetBase &TargetBase::addTarget2(const TargetBaseTypePtr &t, const PackagePath &Name, const Version &V)
{
    auto N = constructTargetName(Name);

    t->pkg = std::make_unique<LocalPackage>(getSolution()->swctx.getLocalStorage(), N, V);

    // this relaxes our requirements, reconsider?
    /*if (getSolution()->isKnownTarget(t->pkg))
    {
        auto i = getSolution()->dummy_children.find(t->pkg);
        if (i != getSolution()->dummy_children.end())
        {
            // we are adding same target for the second time
            // if it was in dummy_children, we remove and re-create it
            //getSolution()->dummy_children.erase(i);

            // we are adding same target for the second time
            // if it was in dummy_children, we add it to children and simply return reference to it
            addChild(i->second);
            getSolution()->dummy_children.erase(i);
            return *i->second;
        }
        else
        {
            auto i = getSolution()->children.find(t->pkg);
            if (i != getSolution()->children.end())
            {
                // we are adding same target for the second time
                // if it was in children, we lock it = set PostponeFileResolving
                //i->second->PostponeFileResolving = true;

                // we are adding same target for the second time
                // if it was in children we simply return reference to it
                return *i->second;
            }
        }
    }*/

    // set some general settings, then init, then register
    setupTarget(t.get());

    getSolution()->call_event(*t, CallbackType::CreateTarget);

    auto set_sdir = [&t, this]()
    {
        if (!t->isLocal())
        {
            t->SourceDir = getSolution()->getSourceDir(t->getPackage());
        }

        // set source dir
        if (t->SourceDir.empty())
            //t->SourceDir = SourceDir.empty() ? getSolution()->SourceDir : SourceDir;
            t->SourceDir = getSolution()->SourceDir;

        // try to get solution provided source dir
        if (auto sd = getSolution()->getSourceDir(t->source, t->getPackage().version); sd)
            t->SourceDir = sd.value();
    };

    set_sdir();

    // try to guess, very naive
    if (!IsConfig)
    {
        // do not create projects under storage yourself!
        t->Local = !is_under_root(t->SourceDir, getSolution()->swctx.getLocalStorage().storage_dir_pkg);
    }

    t->setRootDirectory(RootDirectory); // keep root dir growing
    //t->applyRootDirectory();
    //t->SourceDirBase = t->SourceDir;

    while (t->init())
        ;
    addChild(t);

    getSolution()->call_event(*t, CallbackType::CreateTargetInitialized);

    return *t;
}

void TargetBase::addChild(const TargetBaseTypePtr &t)
{
    bool bad_type = t->getType() <= TargetType::Directory;
    // we do not activate targets that are not for current builds
    bool unknown_tgt = /*!IsConfig && */!Local && !getSolution()->isKnownTarget(t->getPackage());
    if (bad_type || unknown_tgt)
    {
        // also disable resolving for such targets
        if (!bad_type && unknown_tgt)
        {
            t->PostponeFileResolving = true;
        }
        getSolution()->dummy_children[t->getPackage()] = t;
    }
    else
        getSolution()->children[t->getPackage()] = t;
}

void TargetBase::setupTarget(TargetBaseType *t) const
{
    bool exists = getSolution()->exists(t->getPackage());
    if (exists)
        throw SW_RUNTIME_ERROR("Target already exists: " + t->getPackage().toString());

    // find automatic way of copying data?

    // lang storage
    //t->languages = languages;
    //t->extensions = extensions;

    // inherit from this
    t->solution = getSolution();
    t->Scope = Scope;
    t->source = source;

    t->IsConfig = IsConfig; // TODO: inherit from reconsider
    t->Local = Local; // TODO: inherit from reconsider
    t->DryRun = DryRun; // TODO: inherit from reconsider
    t->UseStorageBinaryDir = UseStorageBinaryDir; // TODO: inherit from reconsider

    // inherit from solution
    t->PostponeFileResolving = getSolution()->PostponeFileResolving;
    t->ParallelSourceDownload = getSolution()->ParallelSourceDownload;

    //auto p = getSolution()->getKnownTarget(t->getPackage().ppath);
    //if (!p.toString().empty())
}

void TargetBase::add(const TargetBaseTypePtr &t)
{
    t->solution = getSolution();
    addChild(t);
}

bool TargetBase::exists(const PackageId &p) const
{
    throw SW_RUNTIME_ERROR("unreachable code");
}

TargetBase::TargetMap &TargetBase::getChildren()
{
    return getSolution()->getChildren();
}

const TargetBase::TargetMap &TargetBase::getChildren() const
{
    return getSolution()->getChildren();
}

PackagePath TargetBase::constructTargetName(const PackagePath &Name) const
{
    //is_under_root(SourceDir, getDirectories().storage_dir_pkg)
    return NamePrefix / (solution ? this->getPackage().ppath / Name : Name);
}

Solution *TargetBase::getSolution()
{
    return (Solution *)(solution ? solution : this);
}

const Solution *TargetBase::getSolution() const
{
    return solution ? solution : (const Solution *)this;
}

void TargetBase::setRootDirectory(const path &p)
{
    // FIXME: add root dir to idirs?

    // set always
    RootDirectory = p;
    applyRootDirectory();
}

void TargetBase::setSource(const Source &s)
{
    source = s;

    // apply some defaults
    if (auto g = std::get_if<Git>(&source); g && !g->isValid())
    {
        if (getPackage().version.isBranch())
        {
            if (g->branch.empty())
                g->branch = "{v}";
        }
        else
        {
            if (g->tag.empty())
                g->tag = "{v}";
        }
    }

    auto d = getSolution()->fetch_dir;
    if (d.empty()/* || !ParallelSourceDownload*/ || !isLocal())
        return;

    auto s2 = source; // make a copy!
    checkSourceAndVersion(s2, getPackage().getVersion());
    d /= get_source_hash(s2);

    if (!fs::exists(d))
    {
        LOG_INFO(logger, "Downloading source:\n" << print_source(s2));
        download(s2, d);
    }
    d = d / findRootDirectory(d); // pass found regex or files for better root dir lookup
    d /= getSolution()->prefix_source_dir;
    getSolution()->source_dirs_by_source[s2] = d;
    /*getSolution()->SourceDir = */SourceDir = d;
}

TargetBase &TargetBase::operator+=(const Source &s)
{
    setSource(s);
    return *this;
}

void TargetBase::operator=(const Source &s)
{
    setSource(s);
}

void TargetBase::applyRootDirectory()
{
    // but append only in some cases
    if (!PostponeFileResolving/* && Local*/)
    {
        // prevent adding last delimeter
        if (!RootDirectory.empty())
            SourceDir /= RootDirectory;
    }
}

String TargetBase::getConfig(bool use_short_config) const
{
    return getSolution()->Settings.getConfig(this, use_short_config);
}

path TargetBase::getBaseDir() const
{
    return getSolution()->BinaryDir / getConfig();
}

path TargetBase::getServiceDir() const
{
    return BinaryDir / "misc";
}

path TargetBase::getTargetsDir() const
{
    return getSolution()->BinaryDir / getConfig() / "targets";
}

path TargetBase::getTargetDirShort(const path &root) const
{
    // make t subdir or tgt? or tgts?
    return root / "t" / getConfig(true) / shorten_hash(blake2b_512(getPackage().toString()), 6);
}

path TargetBase::getTempDir() const
{
    return getServiceDir() / "temp";
}

void TargetBase::fetch()
{
    if (PostponeFileResolving || DryRun)
        return;

    static SourceDirMap fetched_dirs;
    auto i = fetched_dirs.find(source);
    if (i == fetched_dirs.end())
    {
        path d = get_source_hash(source);
        d = BinaryDir / d;
        if (!fs::exists(d))
        {
            applyVersionToUrl(source, getPackage().version);
            download(source, d);
        }
        d = d / findRootDirectory(d);
        SourceDir = d;

        fetched_dirs[source] = d;
    }
    else
    {
        SourceDir = i->second;
    }
}

int TargetBase::getCommandStorageType() const
{
    if (getSolution()->command_storage == builder::Command::CS_DO_NOT_SAVE)
        return builder::Command::CS_DO_NOT_SAVE;
    return (isLocal() && !IsConfig) ? builder::Command::CS_LOCAL : builder::Command::CS_GLOBAL;
}

bool TargetBase::isLocal() const
{
    return Local && !getPackage().getOverriddenDir();
}

const LocalPackage &TargetBase::getPackage() const
{
    if (!pkg)
        throw SW_LOGIC_ERROR("pkg not created");
    return *pkg;
}

LocalPackage &TargetBase::getPackageMutable()
{
    if (!pkg)
        throw SW_LOGIC_ERROR("pkg not created");
    return *pkg;
}

Commands Target::getCommands() const
{
    auto cmds = getCommands1();
    for (auto &c : cmds)
        c->command_storage = getCommandStorageType();
    return cmds;
}

void Target::registerCommand(builder::Command &c) const
{
    c.command_storage = getCommandStorageType();
}

void Target::removeFile(const path &fn, bool binary_dir)
{
    auto p = fn;
    if (!p.is_absolute())
    {
        if (!binary_dir && fs::exists(SourceDir / p))
            p = SourceDir / p;
        else if (fs::exists(BinaryDir / p))
            p = BinaryDir / p;
    }

    error_code ec;
    fs::remove(p, ec);
}

bool Target::init()
{
    auto get_config_with_deps = [this]() -> String
    {
        StringSet ss;
        /*for (const auto &[unr, res] : getPackageStore().resolved_packages)
        {
            if (res == getPackage())
            {
                for (const auto &[ppath, dep] : res.db_dependencies)
                    ss.insert(dep.toString());
                break;
            }
        }*/
        String s;
        for (auto &v : ss)
            s += v + "\n";
        bool short_config = true;
        auto c = getConfig(short_config);
        //if (!s.empty())
            //addConfigElement(c, s);
        c = hashConfig(c, short_config);
        return c;
    };

    if (SW_IS_LOCAL_BINARY_DIR)
    {
        BinaryDir = getTargetDirShort(getSolution()->BinaryDir);
    }
    else if (auto d = getPackage().getOverriddenDir(); d)
    {
        // same as local for testing purposes?
        BinaryDir = getTargetDirShort(d.value() / SW_BINARY_DIR);

        //BinaryDir = d.value() / SW_BINARY_DIR;
        //BinaryDir /= sha256_short(getPackage().toString()); // getPackage() first
        //BinaryDir /= path(getConfig(true));
    }
    else /* package from network */
    {
        BinaryDir = getObjectDir(getPackage(), get_config_with_deps()); // remove 'build' part?
    }

    if (DryRun)
    {
        // we doing some download on server or whatever
        // so, we do not want to touch real existing bdirs
        BinaryDir = getSolution()->BinaryDir / "dry" / shorten_hash(blake2b_512(BinaryDir.u8string()), 6);
        fs::remove_all(BinaryDir);
        fs::create_directories(BinaryDir);
    }

    BinaryPrivateDir = BinaryDir / SW_BDIR_PRIVATE_NAME;
    BinaryDir /= SW_BDIR_NAME;

    // we must create it because users probably want to write to it immediately
    fs::create_directories(BinaryDir);
    fs::create_directories(BinaryPrivateDir);

    // make sure we always use absolute paths
    BinaryDir = fs::absolute(BinaryDir);
    BinaryPrivateDir = fs::absolute(BinaryPrivateDir);

    SW_RETURN_MULTIPASS_END;
}

UnresolvedDependenciesType Target::gatherUnresolvedDependencies() const
{
    UnresolvedDependenciesType deps;
    for (auto &d : gatherDependencies())
    {
        if (/*!getSolution()->resolveTarget(d->package) && */!d->target)
            deps.insert({ d->package, d });
    }
    return deps;
}

DependencyPtr Target::getDependency() const
{
    auto d = std::make_shared<Dependency>(*this);
    return d;
}

void TargetOptions::add(const IncludeDirectory &i)
{
    path idir = i.i;
    if (!idir.is_absolute())
    {
        //&& !fs::exists(idir))
        idir = target->SourceDir / idir;
    }
    IncludeDirectories.insert(idir);
}

void TargetOptions::remove(const IncludeDirectory &i)
{
    path idir = i.i;
    if (!idir.is_absolute() && !fs::exists(idir))
        idir = target->SourceDir / idir;
    IncludeDirectories.erase(idir);
}

void NativeTargetOptionsGroup::add(const Variable &v)
{
    auto p = v.v.find_first_of(" =");
    if (p == v.v.npos)
    {
        Variables[v.v];
        return;
    }
    auto f = v.v.substr(0, p);
    auto s = v.v.substr(p + 1);
    if (s.empty())
        Variables[f];
    else
        Variables[f] = s;
}

void NativeTargetOptionsGroup::remove(const Variable &v)
{
    auto p = v.v.find_first_of(" =");
    if (p == v.v.npos)
    {
        Variables.erase(v.v);
        return;
    }
    Variables.erase(v.v.substr(0, p));
}

Files NativeTargetOptionsGroup::gatherAllFiles() const
{
    // maybe cache result?
    Files files;
    for (int i = toIndex(InheritanceType::Min); i < toIndex(InheritanceType::Max); i++)
    {
        auto s = getInheritanceStorage().raw()[i];
        if (!s)
            continue;
        for (auto &f : *s)
            files.insert(f.first);
    }
    return files;
}

DependenciesType NativeTargetOptionsGroup::gatherDependencies() const
{
    DependenciesType deps;
    for (int i = toIndex(InheritanceType::Min); i < toIndex(InheritanceType::Max); i++)
    {
        auto s = getInheritanceStorage().raw()[i];
        if (!s)
            continue;
        for (auto &d : s->Dependencies)
            deps.insert(d);
    }
    return deps;
}

path NativeTargetOptionsGroup::getFile(const Target &dep, const path &fn)
{
    (*this + dep)->Dummy = true;
    return dep.SourceDir / fn;
}

path NativeTargetOptionsGroup::getFile(const DependencyPtr &dep, const path &fn)
{
    (*this + dep)->Dummy = true;
    return target->getSolution()->swctx.resolve(dep->getPackage()).getDirSrc2() / fn;
}

}