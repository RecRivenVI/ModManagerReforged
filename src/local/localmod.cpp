#include "localmod.h"

#include <QDir>
#include <QJsonObject>
#include <QJsonArray>
#include <iterator>
#include <algorithm>
#include <tuple>
#include <functional>

#include "local/localmodpath.h"
#include "local/idmapper.h"
#include "local/knownfile.h"
#include "local/localfilelinker.h"
#include "curseforge/curseforgeapi.h"
#include "curseforge/curseforgemod.h"
#include "modrinth/modrinthapi.h"
#include "modrinth/modrinthmod.h"
#include "download/downloadmanager.h"
#include "util/tutil.hpp"
#include "config.hpp"
#include "util/mmlogger.h"
#include "util/funcutil.h"

LocalMod::LocalMod(QObject *parent, LocalModFile *file) :
    QObject(parent),
    curseforgeAPI_(CurseforgeAPI::api()),
    modrinthAPI_(ModrinthAPI::api())
{
    setModFile(file);
}

LocalMod::LocalMod(LocalModPath *parent, LocalModFile *file) :
    QObject(parent),
    curseforgeAPI_(parent->curseforgeAPI()),
    modrinthAPI_(parent->modrinthAPI()),
    path_(parent),
    targetVersion_(parent->info().gameVersion()),
    targetLoaderType_(parent->info().loaderType())
{
    addSubTagable(path_);
    setModFile(file);
    connect(parent, &LocalModPath::infoUpdated, this, [=]{
        targetVersion_ = parent->info().gameVersion();
        targetLoaderType_ = parent->info().loaderType();
    });
}

LocalMod::~LocalMod()
{
    MMLogger::dtor(this) << commonInfo()->id();
}

CurseforgeMod *LocalMod::curseforgeMod() const
{
    return curseforgeMod_;
}

void LocalMod::setCurseforgeMod(CurseforgeMod *newCurseforgeMod)
{
    curseforgeMod_ = newCurseforgeMod;
    if(curseforgeMod_){
        curseforgeMod_->setParent(this);
        emit curseforgeReady(true);
        emit modCacheUpdated();
        emit modFileUpdated();
    }
}

void LocalMod::searchOnWebsite()
{
    auto linker = new LocalFileLinker(modFile_);
    connect(linker, &LocalFileLinker::linkFinished, this, &LocalMod::websiteReady);
    connect(linker, &LocalFileLinker::linkCurseforgeFinished, this, &LocalMod::curseforgeReady);
    connect(linker, &LocalFileLinker::curseforgeFileChanged, this, [=](const auto &info){
        setCurrentCurseforgeFileInfo(info, false);
    });
    connect(linker, &LocalFileLinker::linkModrinthFinished, this, &LocalMod::modrinthReady);
    connect(linker, &LocalFileLinker::modrinthFileChanged, this, [=](const auto &info){
        setCurrentModrinthFileInfo(info, false);
    });
    linker->link();
}

void LocalMod::checkUpdates(bool force)
{
    //clear update cache
    modrinthUpdate_.reset();
    curseforgeUpdate_.reset();

    emit checkUpdatesStarted();

    Config config;
    auto count = std::make_shared<int>(0);
    auto doneCount = std::make_shared<int>(0);
    auto success = std::make_shared<bool>(false);
    auto foo = [=](bool hasUpdate[[maybe_unused]], bool success2){
        qDebug() << displayName() << "done:" << ++(*doneCount) << "/" << *count;
        if(success2) *success = true;
        if(*doneCount == *count){
            emit modCacheUpdated();
            emit updateReady(updateTypes(), *success);
        }
    };
    if(config.getUseCurseforgeUpdate() && curseforgeMod_ && curseforgeUpdate_.currentFileInfo()){
        (*count)++;
        checkCurseforgeUpdate(force);
//        connect(this, &LocalMod::checkCancelled, disconnecter(
//        connect(this, &LocalMod::curseforgeUpdateReady, foo);
        connect(this, &LocalMod::curseforgeUpdateReady, [=](bool hasUpdate, bool success2){
            qDebug() << "curseforge finish: " << displayName();
            foo(hasUpdate, success2);
        });
    }
    if(config.getUseModrinthUpdate() && modrinthMod_ && modrinthUpdate_.currentFileInfo()){
        (*count)++;
        checkModrinthUpdate(force);
//        connect(this, &LocalMod::checkCancelled, disconnecter(
//                    connect(this, &LocalMod::modrinthUpdateReady, foo)));
        connect(this, &LocalMod::modrinthUpdateReady, [=](bool hasUpdate, bool success2){
            qDebug() << "modrinth finish: " << displayName();
            foo(hasUpdate, success2);
        });
    }
    if(!*count) emit updateReady({});
}

void LocalMod::cancelChecking()
{
    emit checkCancelled();
}

void LocalMod::checkCurseforgeUpdate(bool force)
{
    if(!curseforgeMod_){
        emit curseforgeUpdateReady(false, false);
        return;
    }

    emit checkCurseforgeUpdateStarted();

    //update file list
    if(force || curseforgeMod_->modInfo().allFileList().isEmpty()){
        connect(this, &LocalMod::checkCancelled, disconnecter(
                    curseforgeMod_->acquireAllFileList([=](const QList<CurseforgeFileInfo> &fileList){
            bool bl = curseforgeUpdate_.findUpdate(fileList, targetVersion_, targetLoaderType_);
            emit curseforgeUpdateReady(bl);
        }, [=]{
            emit curseforgeUpdateReady(false, false);
        })));
    }else{
        bool bl = curseforgeUpdate_.findUpdate(curseforgeMod_->modInfo().allFileList(), targetVersion_, targetLoaderType_);
        emit curseforgeUpdateReady(bl);
    }
}

void LocalMod::checkModrinthUpdate(bool force)
{
    if(!modrinthMod_){
        emit modrinthUpdateReady(false, false);
        return;
    }

    emit checkModrinthUpdateStarted();
    auto updateFullInfo = [=]{
        if(!modrinthMod_->modInfo().fileList().isEmpty()){
            bool bl = modrinthUpdate_.findUpdate(modrinthMod_->modInfo().fileList(), targetVersion_, targetLoaderType_);
            emit modrinthUpdateReady(bl);
        }else {
            connect(this, &LocalMod::checkCancelled, disconnecter(
                        modrinthMod_->acquireFileList([=](const QList<ModrinthFileInfo> &fileList){
                bool bl = modrinthUpdate_.findUpdate(fileList, targetVersion_, targetLoaderType_);
                emit modrinthUpdateReady(bl);
            }, [=]{
                emit modrinthUpdateReady(false, false);
            })));
        }
    };

    //always acquire
//    if(modrinthMod_->modInfo().hasFullInfo())
//        updateFullInfo();
//    else {
    if(force || modrinthMod_->modInfo().fileList().isEmpty()){
        if(modrinthMod_->modInfo().hasFullInfo())
            updateFullInfo();
        else{
            modrinthMod_->acquireFullInfo();
            connect(modrinthMod_, &ModrinthMod::fullInfoReady, this, updateFullInfo);
        }
    }
    //    }
}

QAria2Downloader *LocalMod::downloadOldMod(DownloadFileInfo &info)
{
    //TODO: something wrong
    info.setPath(path_->info().path());
    auto downloader = DownloadManager::manager()->download(info);
    connect(downloader, &AbstractDownloader::finished, this, [=]{
        QFileInfo fileInfo(path_->info().path(), info.fileName());
        if(!LocalModFile::availableSuffix.contains(fileInfo.suffix())) return;
        auto file = new LocalModFile(this, fileInfo.absoluteFilePath());
        file->loadInfo();
        qDebug() << file->addOld();
        addOldFile(file);
        emit modFileUpdated();
    });
    return downloader;
}

ModWebsiteType LocalMod::defaultUpdateType() const
{
    auto list = updateTypes();
    return list.isEmpty()? ModWebsiteType::None : list.first();
}

QList<ModWebsiteType> LocalMod::updateTypes() const
{
    bool bl1(curseforgeUpdate_.hasUpdate());
    bool bl2(modrinthUpdate_.hasUpdate());

    if(bl1 && !bl2)
        return { ModWebsiteType::Curseforge };
    else if(!bl1 && bl2)
        return { ModWebsiteType::Modrinth };
    else if(bl1 && bl2){
        if(curseforgeUpdate_.updateFileInfos().first().fileDate() > modrinthUpdate_.updateFileInfos().first().fileDate())
            return { ModWebsiteType::Curseforge, ModWebsiteType::Modrinth };
        else
            return { ModWebsiteType::Modrinth, ModWebsiteType::Curseforge};
    } else
        return {};
}

QAria2Downloader *LocalMod::update()
{
    switch (defaultUpdateType()) {
    case ModWebsiteType::None:
        return nullptr;
    case ModWebsiteType::Curseforge:
        return update(curseforgeUpdate_.updateFileInfos().first());
    case ModWebsiteType::Modrinth:
        return update(modrinthUpdate_.updateFileInfos().first());
    }
}

CurseforgeAPI *LocalMod::curseforgeAPI() const
{
    return curseforgeAPI_;
}

ModrinthAPI *LocalMod::modrinthAPI() const
{
    return modrinthAPI_;
}

void LocalMod::addOldFile(LocalModFile *oldFile)
{
    oldFile->setParent(this);
    oldFiles_ << oldFile;
    emit modFileUpdated();
}

void LocalMod::addDuplicateFile(LocalModFile *duplicateFile)
{
    duplicateFile->setParent(this);
    duplicateFiles_ << duplicateFile;
}

void LocalMod::duplicateToOld()
{
    for(auto &file : qAsConst(duplicateFiles_)){
        file->addOld();
        oldFiles_ << file;
    }

    duplicateFiles_.clear();
    emit modFileUpdated();
}

void LocalMod::rollback(LocalModFile *file)
{
    auto disabled = isDisabled();
    if(disabled) setEnabled(true);
    file->removeOld();
    oldFiles_.removeAll(file);
    modFile_->addOld();
    oldFiles_ << modFile_;
    setModFile(file);
    if(disabled) setEnabled(false);
    emit modFileUpdated();

    //reset update info
    curseforgeUpdate_.reset(true);
    modrinthUpdate_.reset(true);
    connect(this, &LocalMod::websiteReady, this, [=]{
        checkUpdates(false);
    });
    searchOnWebsite();
}

void LocalMod::deleteAllOld()
{
    for(auto &oldFile : qAsConst(oldFiles_))
        oldFile->remove();
    clearQObjects(oldFiles_);
    emit modFileUpdated();
}

void LocalMod::deleteOld(LocalModFile *file)
{
    if(file->remove()){
        oldFiles_.removeOne(file);
        file->setParent(nullptr);
        file->deleteLater();
    }
    emit modFileUpdated();
}

bool LocalMod::isDisabled()
{
    return modFile_->type() == LocalModFile::Disabled;
}

bool LocalMod::isEnabled()
{
    return !isDisabled();
}

bool LocalMod::setEnabled(bool enabled)
{
    auto bl = modFile_->setEnabled(enabled);
    if(bl){
        emit modFileUpdated();
        updateIcon();
    }
    return bl;
}

void LocalMod::clearIgnores()
{
    curseforgeUpdate_.clearIgnores();
    modrinthUpdate_.clearIgnores();
    emit modCacheUpdated();
    emit updateReady(updateTypes());
    checkUpdates();
}

void LocalMod::addDepend(std::tuple<QString, QString, std::optional<FabricModInfo> > modDepend)
{
    depends_ << modDepend;
}

void LocalMod::addConflict(std::tuple<QString, QString, FabricModInfo> modConflict)
{
    conflicts_ << modConflict;
}

void LocalMod::addBreak(std::tuple<QString, QString, FabricModInfo> modBreak)
{
    breaks_ << modBreak;
}

LocalModFile *LocalMod::modFile() const
{
    return modFile_;
}

const CommonModInfo *LocalMod::commonInfo() const
{
    return modFile_->commonInfo();
}

QString LocalMod::displayName() const
{
    if(!alias_.isEmpty())
        return alias_;
    if(!commonInfo()->name().isEmpty())
        return commonInfo()->name();
    return modFile_->fileInfo().fileName();
}

const QList<LocalModFile *> &LocalMod::oldFiles() const
{
    return oldFiles_;
}

const QList<LocalModFile *> &LocalMod::duplicateFiles() const
{
    return duplicateFiles_;
}

const Updatable<CurseforgeFileInfo> &LocalMod::curseforgeUpdate() const
{
    return curseforgeUpdate_;
}

const Updatable<ModrinthFileInfo> &LocalMod::modrinthUpdate() const
{
    return modrinthUpdate_;
}

void LocalMod::setCurrentCurseforgeFileInfo(const std::optional<CurseforgeFileInfo> &info, bool cache)
{
    if(info->id() == 0) return;
    curseforgeUpdate_.setCurrentFileInfo(info);
    emit modFileUpdated();
    if(cache && info)
        KnownFile::addCurseforge(modFile_->murmurhash(), *info);
}

void LocalMod::setCurrentModrinthFileInfo(const std::optional<ModrinthFileInfo> &info, bool cache)
{
    if(info->id().isEmpty()) return;
    modrinthUpdate_.setCurrentFileInfo(info);
    emit modFileUpdated();
    if(cache && info)
        KnownFile::addModrinth(modFile_->sha1(), *info);
}

const QList<std::tuple<QString, QString, std::optional<FabricModInfo> > > &LocalMod::depends() const
{
    return depends_;
}

const QList<std::tuple<QString, QString, FabricModInfo> > &LocalMod::conflicts() const
{
    return conflicts_;
}

const QList<std::tuple<QString, QString, FabricModInfo> > &LocalMod::breaks() const
{
    return breaks_;
}

void LocalMod::setCurseforgeId(int id, bool cache)
{
    if(id != 0){
        curseforgeMod_ = new CurseforgeMod(this, id);
        emit curseforgeReady(true);
        if(cache)
            IdMapper::addCurseforge(commonInfo()->id(), id);
        updateIcon();
    } else
        emit curseforgeReady(false);
}

void LocalMod::setModrinthId(const QString &id, bool cache)
{
    if(!id.isEmpty()){
        setModrinthMod(new ModrinthMod(this, id));
        if(cache)
            IdMapper::addModrinth(commonInfo()->id(), id);
    } else
        emit modrinthReady(false);
}

const QString &LocalMod::alias() const
{
    return alias_;
}

void LocalMod::setAlias(const QString &newAlias)
{
    if(alias_ == newAlias) return;
    alias_ = newAlias;
    emit modFileUpdated();
    emit modCacheUpdated();
}

QJsonObject LocalMod::toJsonObject() const
{
    QJsonObject object;

    if(!alias_.isEmpty())
        object.insert("alias", alias_);
    if(isFeatured_)
        object.insert("featured", isFeatured_);
    if(!tags(TagCategory::CustomizableCategories).isEmpty()){
        QJsonArray tagArray;
        for(auto &&tag : tags(TagCategory::CustomizableCategories))
            tagArray << tag.toJsonValue();
        object.insert("tags", tagArray);
    }

    if(curseforgeMod_){
        QJsonObject curseforgeObject;
        curseforgeObject.insert("id", curseforgeMod_->modInfo().id());
        if(!curseforgeUpdate_.ignores().isEmpty()){
            QJsonArray ignoreArray;
            for(auto &&fileId : curseforgeUpdate_.ignores())
                ignoreArray << fileId;
            curseforgeObject.insert("ignored-updates", ignoreArray);
        }
        if(!curseforgeUpdate_.updateFileInfos().isEmpty()){
            QJsonArray updateFileInfoArray;
            for(auto &&fileInfo : curseforgeUpdate_.updateFileInfos())
                updateFileInfoArray << fileInfo.toJsonObject();
            curseforgeObject.insert("available-updates", updateFileInfoArray);
        }
        object.insert("curseforge", curseforgeObject);
    }

    if(modrinthMod_){
        QJsonObject modrinthObject;
        modrinthObject.insert("id", modrinthMod_->modInfo().id());
        if(!modrinthUpdate_.ignores().isEmpty()){
            QJsonArray ignoreArray;
            for(auto &&fileId : modrinthUpdate_.ignores())
                ignoreArray << fileId;
            modrinthObject.insert("ignored-updates", ignoreArray);
        }
        if(!modrinthUpdate_.updateFileInfos().isEmpty()){
            QJsonArray updateFileInfoArray;
            for(auto &&fileInfo : modrinthUpdate_.updateFileInfos())
                updateFileInfoArray << fileInfo.toJsonObject();
            modrinthObject.insert("available-updates", updateFileInfoArray);
        }
        object.insert("modrinth", modrinthObject);
    }

    return object;
}

void LocalMod::restore(const QVariant &variant)
{
    alias_ = value(variant, "alias").toString();
    isFeatured_ = value(variant, "featured").toBool();
    for(auto &&tag : value(variant, "tags").toList())
        addTag(Tag::fromVariant(tag));
    if(contains(variant, "curseforge")){
        if(contains(value(variant, "curseforge"), "id"))
            setCurseforgeId(value(variant, "curseforge", "id").toInt());
        if(contains(value(variant, "curseforge"), "ignored-updates"))
            for(auto &&id : value(variant, "curseforge", "ignored-updates").toList())
                curseforgeUpdate_.addIgnore(id.toInt());
        if(contains(value(variant, "curseforge"), "available-updates"))
            for(auto &&fileInfo : value(variant, "curseforge", "available-updates").toList())
                curseforgeUpdate_ << CurseforgeFileInfo::fromVariant(fileInfo);
    }
    if(contains(variant, "modrinth")){
        if(contains(value(variant, "modrinth"), "id"))
            setModrinthId(value(variant, "modrinth", "id").toString());
        if(contains(value(variant, "modrinth"), "ignored-updates"))
            for(auto &&id : value(variant, "modrinth", "ignored-updates").toList())
                modrinthUpdate_.addIgnore(id.toString());
        if(contains(value(variant, "curseforge"), "available-updates"))
            for(auto &&fileInfo : value(variant, "modrinth", "available-updates").toList())
                modrinthUpdate_ << ModrinthFileInfo::fromVariant(fileInfo);
    }
}

bool LocalMod::isFeatured() const
{
    return isFeatured_;
}

void LocalMod::setFeatured(bool featured)
{
    isFeatured_ = featured;
    emit modCacheUpdated();
    emit modFileUpdated();
}

LocalModPath *LocalMod::path() const
{
    return path_;
}

//const QList<Tag> LocalMod::tags() const
//{
//    return tagManager_.tags();
//}

//const QList<Tag> LocalMod::customizableTags() const
//{
//    return tagManager_.tags(TagCategory::CustomizableCategories);
//}

//void LocalMod::addTag(const Tag &tag)
//{
//    tagManager_.addTag(tag);
//    emit modCacheUpdated();
//    emit modFileUpdated();
//}

//void LocalMod::removeTag(const Tag &tag)
//{
//    tagManager_.removeTag(tag);
//    emit modCacheUpdated();
//    emit modFileUpdated();
//}

//Tagable &LocalMod::tagManager()
//{
//    return tagManager_;
//}

void LocalMod::setModFile(LocalModFile *newModFile)
{
    if(modFile_) disconnect(modFile_, &LocalModFile::fileChanged, this, &LocalMod::modFileUpdated);
    removeSubTagable(modFile_);
    modFile_ = newModFile;
    addSubTagable(modFile_);
    if(modFile_) {
        connect(modFile_, &LocalModFile::fileChanged, this, &LocalMod::modFileUpdated);
        modFile_->setParent(this);
        if(modFile_->loaderType() == ModLoaderType::Fabric){
            if(auto environment = modFile_->fabric().environment(); environment == "*"){
                addTag(Tag::clientTag());
                addTag(Tag::serverTag());
            }else if(environment == "client")
                addTag(Tag::clientTag());
            else if(environment == "server")
                addTag(Tag::serverTag());
        }
    }
    updateIcon();
}

const QPixmap &LocalMod::icon() const
{
    return icon_;
}

void LocalMod::updateIcon()
{
    auto applyIcon = [=]{
        if(isDisabled()){
            QImage image(icon_.toImage());
            auto alphaChannel = image.convertToFormat(QImage::Format_Alpha8);
            image = image.convertToFormat(QImage::Format_Grayscale8);
            image.setAlphaChannel(alphaChannel);
            icon_.convertFromImage(image);
        }
        emit modIconUpdated();
    };

    if(!commonInfo()->iconBytes().isEmpty()){
        icon_.loadFromData(commonInfo()->iconBytes());
        applyIcon();
    }else if(commonInfo()->id() == "optifine") {
        icon_.load(":/image/optifine.png");
        applyIcon();
    }else if(curseforgeMod_){
        auto setCurseforgeIcon = [=]{
            icon_.loadFromData(curseforgeMod_->modInfo().iconBytes());
            applyIcon();
        };
        if(!curseforgeMod_->modInfo().iconBytes().isEmpty())
            setCurseforgeIcon();
        else{
            connect(curseforgeMod_, &CurseforgeMod::basicInfoReady, this, [=]{
                connect(curseforgeMod_, &CurseforgeMod::iconReady, this, setCurseforgeIcon);
                curseforgeMod_->acquireIcon();
            });
            curseforgeMod_->acquireBasicInfo();
        }
    } else if(modrinthMod_){
        auto setModrinthIcon = [=]{
            icon_.loadFromData(modrinthMod_->modInfo().iconBytes());
            applyIcon();
        };
        if(!modrinthMod_->modInfo().iconBytes().isEmpty())
            setModrinthIcon();
        else{
            modrinthMod_->acquireFullInfo();
            connect(modrinthMod_, &ModrinthMod::fullInfoReady, this, [=]{
                connect(modrinthMod_, &ModrinthMod::iconReady, this, setModrinthIcon);
                modrinthMod_->acquireIcon();
            });
        }
    } else{
        icon_.load(":/image/modmanager.png");
        applyIcon();
    }
}

ModrinthMod *LocalMod::modrinthMod() const
{
    return modrinthMod_;
}

void LocalMod::setModrinthMod(ModrinthMod *newModrinthMod)
{
    removeSubTagable(modrinthMod_);
    modrinthMod_ = newModrinthMod;
    addSubTagable(modrinthMod_);
    if(modrinthMod_){
        modrinthMod_->setParent(this);
        emit modrinthReady(true);
        emit modCacheUpdated();
        emit modFileUpdated();
    }
}
