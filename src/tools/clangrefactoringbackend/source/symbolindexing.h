/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#pragma once

#include "symbolindexinginterface.h"

#include "storagesqlitestatementfactory.h"
#include "symbolindexer.h"
#include "symbolscollector.h"
#include "processormanager.h"
#include "symbolindexertaskqueue.h"
#include "taskscheduler.h"
#include "symbolstorage.h"

#include <refactoringdatabaseinitializer.h>
#include <filepathcachingfwd.h>

#include <sqlitedatabase.h>
#include <sqlitereadstatement.h>
#include <sqlitewritestatement.h>

#include <QFileSystemWatcher>

#include <thread>

namespace ClangBackEnd {

class SymbolsCollectorManager;
class RefactoringServer;

class SymbolsCollectorManager final : public ClangBackEnd::ProcessorManager<SymbolsCollector>
{
public:
    using Processor = SymbolsCollector;
    SymbolsCollectorManager(const ClangBackEnd::GeneratedFiles &generatedFiles,
                            Sqlite::Database &database)
        : ProcessorManager(generatedFiles),
          m_database(database)
    {}

protected:
    std::unique_ptr<SymbolsCollector> createProcessor() const
    {
        return  std::make_unique<SymbolsCollector>(m_database);
    }

private:
    Sqlite::Database &m_database;
};

class SymbolIndexing final : public SymbolIndexingInterface
{
public:
    using StatementFactory = ClangBackEnd::StorageSqliteStatementFactory<Sqlite::Database>;
    using Storage = ClangBackEnd::SymbolStorage<StatementFactory>;
    SymbolIndexing(Sqlite::Database &database,
                   FilePathCachingInterface &filePathCache,
                   const GeneratedFiles &generatedFiles,
                   ProgressCounter::SetProgressCallback &&setProgressCallback)
        : m_filePathCache(filePathCache),
          m_statementFactory(database),
          m_collectorManger(generatedFiles, database),
          m_progressCounter(std::move(setProgressCallback)),
          m_indexerScheduler(m_collectorManger, m_indexerQueue, m_progressCounter, std::thread::hardware_concurrency())
    {
    }

    ~SymbolIndexing()
    {
        syncTasks();
    }

    SymbolIndexer &indexer()
    {
        return m_indexer;
    }

    void syncTasks()
    {
        m_indexerScheduler.disable();
        while (!m_indexerScheduler.futures().empty()) {
            m_indexerScheduler.syncTasks();
            m_indexerScheduler.freeSlots();
        }
    }

    void updateProjectParts(V2::ProjectPartContainers &&projectParts) override;

private:
    using SymbolIndexerTaskScheduler = TaskScheduler<SymbolsCollectorManager, SymbolIndexerTask::Callable>;
    FilePathCachingInterface &m_filePathCache;
    StatementFactory m_statementFactory;
    Storage m_symbolStorage{m_statementFactory};
    ClangPathWatcher<QFileSystemWatcher, QTimer> m_sourceWatcher{m_filePathCache};
    FileStatusCache m_fileStatusCache{m_filePathCache};
    SymbolsCollectorManager m_collectorManger;
    ProgressCounter m_progressCounter;
    SymbolIndexerTaskScheduler m_indexerScheduler;
    SymbolIndexerTaskQueue m_indexerQueue{m_indexerScheduler, m_progressCounter};
    SymbolIndexer m_indexer{m_indexerQueue,
                            m_symbolStorage,
                            m_sourceWatcher,
                            m_filePathCache,
                            m_fileStatusCache,
                            m_statementFactory.database};
};

} // namespace ClangBackEnd
