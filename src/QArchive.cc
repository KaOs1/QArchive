/*
 * BSD 3-Clause License
 *
 * Copyright (c) 2018, Antony jr
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * -----------------------------------------------------------------------------
 *  @filename 	 	: QArchive.cc
 *  @description 	: C++ Cross-Platform helper library that Modernizes libarchive
 *  			  using Qt5. Simply extracts 7z  , Tarballs  , RAR
 *  			  and other supported formats by libarchive.
 *  			  The Source file.
 * -----------------------------------------------------------------------------
*/
#include "../include/QArchive.hpp"

/*
 * Destructors for struct archive.
 * Which is Handled by smart pointers.
*/
static void deleteArchiveReader(struct archive *ar)
{
    if(ar){
        archive_read_close(ar);
        archive_read_free(ar);
    }
    return;
}

static void deleteArchiveWriter(struct archive *aw)
{
    if(aw){
        archive_write_close(aw);
        archive_write_free(aw);
    }
    return;
}

static void deleteArchiveEntry(struct archive_entry *entry)
{
    if(entry){
        archive_entry_free(entry);
    }
    return;
}

// ---


using namespace QArchive;

/*
 * Extractor Class Source.
 * ----------------------------
 * Constructor and Destructor.
 * ----------------------------
*/
Extractor::Extractor(QObject *parent)
    : QObject(parent)
{
    Watcher = new QFutureWatcher<void>(this);
    connect(Watcher, &QFutureWatcher<void>::started, this, &Extractor::started);
    connect(Watcher, &QFutureWatcher<void>::paused, this, &Extractor::paused);
    connect(Watcher, &QFutureWatcher<void>::resumed, this, &Extractor::resumed);
    connect(Watcher, &QFutureWatcher<void>::canceled, this, &Extractor::canceled);
    connect(Watcher, &QFutureWatcher<void>::finished, this, &Extractor::finished);
    connect(Watcher, &QFutureWatcher<void>::progressValueChanged, this, &Extractor::progress);
    return;
}

Extractor::Extractor(const QString &filename)
    : QObject(nullptr)
{
    Watcher = new QFutureWatcher<void>(this);
    connect(Watcher, &QFutureWatcher<void>::started, this, &Extractor::started);
    connect(Watcher, &QFutureWatcher<void>::paused, this, &Extractor::paused);
    connect(Watcher, &QFutureWatcher<void>::resumed, this, &Extractor::resumed);
    connect(Watcher, &QFutureWatcher<void>::canceled, this, &Extractor::canceled);
    connect(Watcher, &QFutureWatcher<void>::finished, this, &Extractor::finished);
    connect(Watcher, &QFutureWatcher<void>::progressValueChanged, this, &Extractor::progress);
    setArchive(filename);
    return;
}

Extractor::Extractor(const QString &filename, const QString &destination)
    : QObject(nullptr)
{
    Watcher = new QFutureWatcher<void>(this);
    connect(Watcher, &QFutureWatcher<void>::started, this, &Extractor::started);
    connect(Watcher, &QFutureWatcher<void>::paused, this, &Extractor::paused);
    connect(Watcher, &QFutureWatcher<void>::resumed, this, &Extractor::resumed);
    connect(Watcher, &QFutureWatcher<void>::canceled, this, &Extractor::canceled);
    connect(Watcher, &QFutureWatcher<void>::finished, this, &Extractor::finished);
    connect(Watcher, &QFutureWatcher<void>::progressValueChanged, this, &Extractor::progress);
    setArchive(filename, destination);
    return;
}

Extractor::~Extractor()
{
    if(isRunning()) {
        cancel();
        waitForFinished();
        Watcher->disconnect();
        // blocks any-thread until every
        // job is stopped successfully ,
        // Otherwise this can cause a
        // segfault.
    }
    return;
}

/* ------ */

/*
 * Public Methods.
 * ---------------
 *  Extractor
*/
Extractor &Extractor::setArchive(const QString &filename)
{
    QMutexLocker locker(&mutex);
    ArchivePath = QString(filename);
    return *this;
}

Extractor &Extractor::setArchive(const QString &filename, const QString &destination)
{
    QMutexLocker locker(&mutex);
    ArchivePath = QString(filename);
    Destination = QString(destination);
    return *this;
}

Extractor &Extractor::onlyExtract(const QString &filepath)
{
    QMutexLocker locker(&mutex);
    if(OnlyExtract.contains(filepath)){
        return *this;
    }
    OnlyExtract << (Destination + filepath);
    return *this;
}

Extractor &Extractor::onlyExtract(const QStringList &filepaths)
{
    QMutexLocker locker(&mutex);
    Q_FOREACH(QString filepath , filepaths){
        if(!OnlyExtract.contains(filepath)){
            OnlyExtract << (Destination + filepath);
        }
    }
    return *this;
}

/* ------ */

/*
 * Public Slots.
 * ---------------
 *  Extractor
*/
Extractor &Extractor::waitForFinished(void)
{
    Watcher->waitForFinished(); // Sync.
    return *this;
}

Extractor &Extractor::start(void)
{
    if(!mutex.tryLock()){
        return *this;
    }

    if(isPaused()) {
        // If the user called this to
        // resume by mistake.
        mutex.unlock();
        resume();
        return *this;
    }

    // Check if the archive and the destination folder exists
    QFileInfo check_archive(ArchivePath);
    QFileInfo check_dest(Destination);
    // check if file exists and if yes then is it really a file and no directory?
    if (!check_archive.exists() || !check_archive.isFile()) {
        mutex.unlock();
        emit(error(ARCHIVE_READ_ERROR, ArchivePath));
        return *this;
    } else {
        if(!Destination.isEmpty()) {
        if(!check_dest.exists() || check_dest.isDir()) {
            mutex.unlock();
            emit(error(DESTINATION_NOT_FOUND, Destination));
            return *this;
            }
        }
    }
    // ---

    int ret = 0;
    archive = QSharedPointer<struct archive>(archive_read_new(), deleteArchiveReader);
    ext = QSharedPointer<struct archive>(archive_write_disk_new(), deleteArchiveWriter);
    if(!(archive.data() && ext.data())){
        // No memory.
        mutex.unlock();
        emit(error(NOT_ENOUGH_MEMORY , ArchivePath));
        return *this;
    }
    archive_write_disk_set_options(ext.data(), ARCHIVE_EXTRACT_TIME);
    archive_read_support_format_all(archive.data());
    archive_read_support_filter_all(archive.data());

    if((ret = archive_read_open_filename(archive.data(), ArchivePath.toUtf8().constData()  , BlockSize))) {
         mutex.unlock();
         emit(error(ARCHIVE_READ_ERROR, ArchivePath));
         return *this;
     }

    /*
     * Using infinite loop can be dangerous for
     * the gui but calculatively , This is done
     * in blinding speed and I hope that goes for
     * large archive too.
     * Note: We don't do the extraction, We are just 
     * getting the entries while staying in the
     * current thread.
    */
    for (;;){
         QSharedPointer<struct archive_entry> entry = QSharedPointer<struct archive_entry>(archive_entry_new() , deleteArchiveEntry);
         ret = archive_read_next_header2(archive.data(), entry);
         if (ret == ARCHIVE_EOF) {
             break;
         }else if (ret == ARCHIVE_FATAL) {
             mutex.unlock();
             emit(error(ARCHIVE_QUALITY_ERROR, ArchivePath));
             return *this;
         }else{
             entries.append(entry);
             continue;
         }
    }
    // ---

    QFuture<void> *future = new QFuture<void>;
    mutex.unlock();
    *future = QtConcurrent::map(entries ,[this](QSharedPointer<struct archive_entry> entry){
            QMutexLocker locker(&mutex);
            if(!Destination.isEmpty()) {
                char* new_entry = concat(Destination.toUtf8().constData(), archive_entry_pathname(entry.data()));
                archive_entry_set_pathname(entry.data(), new_entry);
                free(new_entry);
            }

            int ret = 0;
            QString checkfile = QString(archive_entry_pathname(entry.data()));
            bool extract = (!OnlyExtract.isEmpty() && !OnlyExtract.contains(checkfile)) ? false : true;
            if(extract) {

            // Signal that we are extracting this file.
             emit(extracting(checkfile));
            // ---

            ret = archive_write_header(ext.data(), entry.data());
            if (ret == ARCHIVE_OK) {
                {
                    const void *buff;
                    size_t size;
#if ARCHIVE_VERSION_NUMBER >= 3000000
                    int64_t offset;
#else
                    off_t offset;
#endif
                    for (ret = 0;;){
                        ret = archive_read_data_block(archive.data(), &buff, &size, &offset);
                        if (ret == ARCHIVE_EOF) {
                            break;
                        } else if (ret != ARCHIVE_OK) {
                            break;
                        } else {
                            ret = archive_write_data_block(ext.data(), buff, size, offset);
                            if (ret != ARCHIVE_OK) {
                                break;
                            }
                        }
                    }
                }
                ret = archive_write_finish_entry(ext.data());
                if (ret == ARCHIVE_FATAL) {
                    emit(error(ARCHIVE_UNCAUGHT_ERROR, checkfile));
                    return;
                }
                
                // Signal that we successfully extracted this file.
                emit(extracted(checkfile));
                // ---
            }
        
        }
        return;
    });
    Watcher->setFuture(*future);
    return *this;
}

Extractor &Extractor::pause(void)
{
    Watcher->pause();
    return *this;
}

Extractor &Extractor::resume(void)
{
    Watcher->resume();
    return *this;
}

Extractor &Extractor::cancel(void)
{
    Watcher->cancel();
    return *this;
}

bool Extractor::isRunning() const
{
    return Watcher->isRunning();
}
bool Extractor::isCanceled() const
{
    return Watcher->isCanceled();
}
bool Extractor::isPaused() const
{
    return Watcher->isPaused();
}
bool Extractor::isStarted() const
{
    return Watcher->isStarted();
}

Extractor &Extractor::setFunc(short signal, std::function<void(void)> function)
{
    switch(signal) {
    case STARTED:
        connect(this, &Extractor::started, function);
        break;
    case FINISHED:
        connect(this, &Extractor::finished, function);
        break;
    case PAUSED:
        connect(this, &Extractor::paused, function);
        break;
    case RESUMED:
        connect(this, &Extractor::resumed, function);
        break;
    case CANCELED:
        connect(this, &Extractor::canceled, function);
        break;
    default:
        break;
    };
    return *this;
}

Extractor &Extractor::setFunc(short signal, std::function<void(QString)> function)
{
    if(signal == EXTRACTED) {
        connect(this, &Extractor::extracted, function);
    } else {
        connect(this, &Extractor::extracting, function);
    }
    return *this;
}

Extractor &Extractor::setFunc(std::function<void(short,QString)> function)
{
    connect(this, &Extractor::error, function);
    return *this;
}

Extractor &Extractor::setFunc(std::function<void(int)> function)
{
    connect(this, &Extractor::progress, function);
    return *this;
}

/* ------ */

/*
 * Private Slots.
 * ---------------
 *  Extractor
 *  Internals for the extractor.
*/

QString Extractor::cleanDestPath(const QString& input)
{
    QString ret = QDir::cleanPath(QDir::toNativeSeparators(input));
    if(ret.at(ret.count() - 1) != QDir::separator()) {
        ret += QDir::separator();
    }
    return ret;
}

char *Extractor::concat(const char *dest, const char *src)
{
    char *ret = (char*) calloc(sizeof(char), strlen(dest) + strlen(src) + 1);

    strcpy(ret, dest);
    strcat(ret, src);

    return ret;
}

/* ------ //
 *  Class Extractor Ends Here.
 * -----------------------------
*/


/*
 * Class Compressor
 * --------------------------
 *  QArchive Compressor Source
 * -----------------------------
 * Constructor and Destructor.
 * -----------------------------
*/
Compressor::Compressor(QObject *parent)
    : QObject(parent)
{
    connectWatcher();
    return;
}

Compressor::Compressor(const QString& archive)
    : QObject(nullptr)
{
    connectWatcher();
    setArchive(archive);
    return;
}

Compressor::Compressor(const QString& archive, const QStringList& files)
    : QObject(nullptr)
{
    connectWatcher();
    setArchive(archive);
    addFiles(files);
    return;

}

Compressor::Compressor(const QString& archive, const QString& file)
    : QObject(nullptr)
{
    connectWatcher();
    setArchive(archive);;
    addFiles(file);
    return;
}

Compressor::~Compressor()
{
    if(isRunning()){
        cancel();
        waitForFinished();
        mutex.unlock();
    }
    return;
}

/* ------ */



/*
 * Public Methods.
 * -----------------
 *  Compressor.
*/
Compressor &Compressor::setArchive(const QString &archive)
{
    QMutexLocker locker(&mutex);
    archivePath = QDir::cleanPath(archive);
    return *this;
}

Compressor &Compressor::setArchiveFormat(short type)
{
    QMutexLocker locker(&mutex);
    archiveFormat = type;
    return *this;
}

Compressor &Compressor::addFiles(const QString& file)
{
    /*
     * No like files can exist in a filesystem!
    */
    QMutexLocker locker(&mutex);
    if(!nodes.contains(file)){
        QFileInfo checkfile(file);
        if(checkfile.isFile()){
            nodes.insert( file , checkfile.fileName());
        }else{
             QDir dir(file);
              QFileInfoList list = dir.entryInfoList(QDir::AllEntries);
              for (int i = 0; i < list.size(); i++)
              {
                 if(list.at(i).filePath() != "." || list.at(i).filePath() != ".."){
                         QString filep = list.at(i).filePath();
                         nodes.insert( filep , filep );
                 }
              }
        }
    }
    return *this;
}

Compressor &Compressor::addFiles(const QStringList& files)
{
    QMutexLocker locker(&mutex);
    Q_FOREACH(QString file , files)
    {
        if(!nodes.contains(file)){
         QFileInfo checkfile(file);
         if(checkfile.isFile()){
             nodes.insert( file , checkfile.fileName());
         }else{
             QDir dir(file);
             QFileInfoList list = dir.entryInfoList(QDir::AllEntries);
             for (int i = 0; i < list.size(); i++)
             {
                if(list.at(i).filePath() != "." || list.at(i).filePath() != ".."){
                        QString filep = list.at(i).filePath();
                        nodes.insert( filep , filep );
                }
             }
         }
        }
    }
    return *this;
}

Compressor &Compressor::removeFiles(const QString& file)
{
    QMutexLocker locker(&mutex);
    nodes.remove(file);
    return *this;
}

Compressor &Compressor::removeFiles(const QStringList& files)
{
    QMutexLocker locker(&mutex);
    if(nodes.isEmpty()){
        return *this;
    }
    Q_FOREACH(QString file , files)
    {
        nodes.remove(file);
    }
    return *this;
}

/* ------ */


/*
 * Public Slots.
 * -------------
 *  Compressor
*/
Compressor &Compressor::waitForFinished(void)
{
    watcher.waitForFinished(); // Sync.
    return *this;
}

Compressor &Compressor::start(void)
{
    if(isPaused()){
        resume();
        return *this;
    }
    mutex.lock();
    checkNodes(); // clear unwanted files
    if(nodes.isEmpty() || archivePath.isEmpty()) {
         mutex.unlock();
         emit(finished());
         return *this;
    }
    if(archiveFormat == NO_FORMAT){
         getArchiveFormat();
    }
    bool noTar = false;
    archive =  QSharedPointer<struct archive>(archive_write_new(), deleteArchiveWriter);
    
    switch (archiveFormat) {
     case BZIP:
     case BZIP2:
     case NO_FORMAT:
         archive_write_add_filter_bzip2(archive.data());
         break;
     case GZIP:
         archive_write_add_filter_gzip(archive.data());
         break;
     default:
         noTar = true;
         archive_write_add_filter_none(archive.data());
         break;
     }
     if(noTar) {
         if(archiveFormat == SEVEN_ZIP) {
             archive_write_set_format_7zip(archive.data());
         }
         if(archiveFormat == RAR) {
             archive_write_set_format_xar(archive.data());
         }
         if(archiveFormat == ZIP) {
             archive_write_set_format_zip(archive.data());
         }
     } else {
         archive_write_set_format_ustar(archive.data());
     }
    archive_write_open_filename(archive.data(), archivePath.toStdString().c_str());
    QFuture<void> *future = new QFuture<void>;
    mutex.unlock();

    // Copy the keys to the heap or else
    // this will cause a segfault.
    QList<QString> *keys = new QList<QString>;
    QList<QString> copy = nodes.uniqueKeys();
    keys->swap(copy);
    // ---

    *future = QtConcurrent::map(*keys, [this](QString key){        
            compress_node(key , nodes.value(key));
            return;
    });
    watcher.setFuture(*future);
    return *this;
}

Compressor &Compressor::pause(void)
{
     watcher.pause();
     return *this;
}

Compressor &Compressor::resume(void)
{
     watcher.resume();
     return *this;
}

Compressor &Compressor::cancel(void)
{
     watcher.cancel();
     return *this;
}

bool Compressor::isRunning() const
{
     return watcher.isRunning();
 }
 bool Compressor::isCanceled() const
 {
     return watcher.isCanceled();
 }
 bool Compressor::isPaused() const
 {
     return watcher.isPaused();
 }
 bool Compressor::isStarted() const
 {
     return watcher.isStarted();
 }

 Compressor &Compressor::setFunc(short signal, std::function<void(void)> function)
 {
     switch(signal) {
     case STARTED:
         connect(this, &Compressor::started, function);
         break;
     case FINISHED:
         connect(this, &Compressor::finished, function);
         break;
     case PAUSED:
         connect(this, &Compressor::paused, function);
         break;
     case RESUMED:
         connect(this, &Compressor::resumed, function);
         break;
     case CANCELED:
         connect(this, &Compressor::canceled, function);
         break;
     default:
         break;
     };
     return *this;
 }

 Compressor &Compressor::setFunc(short signal, std::function<void(QString)> function)
 {
     if(signal == COMPRESSED) {
         connect(this, &Compressor::compressed, function);
     } else {
         connect(this, &Compressor::compressing, function);
     }
     return *this;
 }

 Compressor &Compressor::setFunc(std::function<void(short,QString)> function)
 {
     connect(this, &Compressor::error, function);
     return *this;
 }

 Compressor &Compressor::setFunc(std::function<void(int)> function)
 {
     connect(this, &Compressor::progress, function);
     return *this;
 }

/* ------ */

/*
 * Private Slots.
 * ---------------
 *  Compressor
*/
void Compressor::connectWatcher(void)
{
     connect(&watcher, &QFutureWatcher<void>::started, this, &Compressor::started);
     connect(&watcher, &QFutureWatcher<void>::paused, this, &Compressor::paused);
     connect(&watcher, &QFutureWatcher<void>::resumed, this, &Compressor::resumed);
     connect(&watcher, &QFutureWatcher<void>::canceled, this, &Compressor::canceled);
     connect(&watcher, &QFutureWatcher<void>::finished, this, &Compressor::finished);
     connect(&watcher, &QFutureWatcher<void>::progressValueChanged, this, &Compressor::progress);
     return;
}

void Compressor::checkNodes(void)
{
    Q_FOREACH(QString key , nodes.uniqueKeys()){
        QFileInfo fInfo(key);
        if(!fInfo.exists()){
            nodes.remove(key);
            emit error(FILE_NOT_EXIST, key);
        }
    }
    return;
}

void Compressor::getArchiveFormat()
{
    QFileInfo fInfo(archivePath);
    QString ext = fInfo.suffix();

    if(ext.toLower() == "bz") {
        archiveFormat = BZIP;
    }
    if(ext.toLower() == "bz2") {
        archiveFormat = BZIP2;
    } else if(ext.toLower() == "gz") {
        archiveFormat = GZIP;
    } else if(ext.toLower() == "cpio") {
        archiveFormat = CPIO;
    } else if(ext.toLower() == "rar") {
        archiveFormat = RAR;
    } else if(ext.toLower() == "zip") {
        archiveFormat = ZIP;
    } else if(ext.toLower() == "7z") {
        archiveFormat = SEVEN_ZIP;
    } else {
        archiveFormat = NO_FORMAT; // default
    }
}

void Compressor::compress_node(QString filepath , QString entrypath)
{
    QMutexLocker locker(&mutex);
    int r;
    int fd;
    ssize_t len;
    char buff[16384];

    // Signal that we are compressing this file. 
    emit(compressing(filepath));

    QSharedPointer<struct archive> disk = QSharedPointer<struct archive>(archive_read_disk_new(), deleteArchiveReader);
    archive_read_disk_set_standard_lookup(disk.data());

    r = archive_read_disk_open(disk.data(), filepath.toStdString().c_str());
    if (r != ARCHIVE_OK) {
            emit error(DISK_OPEN_ERROR, filepath);
            return;
    }

    for (;;) {
        struct archive_entry *entry = archive_entry_new();
        r = archive_read_next_header2(disk.data(), entry);
        if (r == ARCHIVE_EOF){
                break;
        }
        if (r != ARCHIVE_OK) {
                emit error(DISK_READ_ERROR, filepath);
                return;
        }
        archive_read_disk_descend(disk.data());
        archive_entry_set_pathname(entry , entrypath.toStdString().c_str());
        r = archive_write_header(archive.data() , entry);

        if (r == ARCHIVE_FATAL) {
            emit error(ARCHIVE_FATAL_ERROR, filepath);
            return;
        }
        if (r > ARCHIVE_FAILED) {
                /* For now, we use a simpler loop to copy data
                 * into the target archive. */
                fd = open(filepath.toStdString().c_str(), O_RDONLY);
                len = read(fd, buff, sizeof(buff));
                while (len > 0) {
                    archive_write_data(archive.data(), buff, len);
                    len = read(fd, buff, sizeof(buff));
                }
                close(fd);
        }
        archive_entry_free(entry);
    }

    // Signal that we compressed this file.
    emit compressed(filepath);
    return;
}

/* ------ //
 *  Class Compressor Ends Here.
 * -------------------------------
*/


/*
 * Class Reader.
 * ------------------------
 *  QArchive Reader Source.
 * ---------------------------
 * Constructor and Destructor.
 * ---------------------------
*/
Reader::Reader(QObject *parent)
    : QObject(parent)
{
    return;
}
Reader::Reader(const QString& archive)
    : QObject(nullptr)
{
    setArchive(archive);
    return;
}

Reader::~Reader()
{
    if(Promise != nullptr) {
        stop();
    }
    return;
}

/* ------ */

/*
 * Public Method.
 * --------------
 *  Reader
*/
void Reader::setArchive(const QString& archive)
{
    if(mutex.tryLock()) {
        Archive = QDir::cleanPath(archive);
        mutex.unlock();
    }
    return;
}

const QStringList& Reader::listFiles()
{
    return Files;
}

void Reader::clear()
{
    if(mutex.tryLock()) {
        Archive.clear();
        Files.clear();
        mutex.unlock();
    }
    return;
}

/* ------ */

/*
 * Public Slots.
 * --------------
 *  Reader
*/
bool Reader::isRunning() const
{
    return Promise->isRunning();
}

void Reader::start(void)
{
    if(!mutex.tryLock()) {
        return;
    }
    Promise = new QFuture<void>;
    *Promise = QtConcurrent::run(this, &Reader::startReading);
    return;
}

void Reader::stop(void)
{
    /*
     * If mutex is not locked then that means
     * there is no start called or the operation is
     * finished , so doing stop is useless.
     */
    if(mutex.tryLock() && !Promise->isRunning()) {
        mutex.unlock();
        return;
    }
    stopReader = true;
    return;
}

/* ------ */

/*
 * Private Slots.
 * ---------------
 *  Reader
*/
void Reader::startReading()
{
    if(Archive.isEmpty()) {
        mutex.unlock();
        return;
    }

    QFileInfo fInfo(Archive);
    if(!fInfo.exists()) {
        mutex.unlock();
        emit error(ARCHIVE_READ_ERROR, Archive);
        return;
    }

    struct archive *arch;
    struct archive_entry *entry;
    int ret = 0;

    arch = archive_read_new();
    archive_read_support_format_all(arch);
    archive_read_support_filter_all(arch);

    if((ret = archive_read_open_filename(arch, Archive.toStdString().c_str(), 10240))) {
        mutex.unlock();
        emit error(ARCHIVE_READ_ERROR, Archive);
        return;
    }
    for (; !stopReader;) {
        ret = archive_read_next_header(arch, &entry);
        if (ret == ARCHIVE_EOF) {
            break;
        }
        if (ret != ARCHIVE_OK) {
            mutex.unlock();
            emit error(ARCHIVE_QUALITY_ERROR, Archive);
            return;
        }
        Files << archive_entry_pathname(entry);
    }
    archive_read_close(arch);
    archive_read_free(arch);
    mutex.unlock();
    if(stopReader) {
        emit(stopped());
        return;
    }
    emit archiveFiles(Archive, Files);
    return;
}

/* ------ //
 *  Class Reader Ends Here.
 * ------------------------------------
*/

// =============================================================== //
