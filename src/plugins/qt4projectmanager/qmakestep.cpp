/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://www.qtsoftware.com/contact.
**
**************************************************************************/

#include "qmakestep.h"

#include "qt4project.h"
#include "qt4projectmanagerconstants.h"
#include "qt4projectmanager.h"
#include "makestep.h"
#include "qtversionmanager.h"

#include <coreplugin/icore.h>
#include <utils/qtcassert.h>

#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QCoreApplication>

using namespace Qt4ProjectManager;
using namespace Qt4ProjectManager::Internal;
using namespace ProjectExplorer;

QMakeStep::QMakeStep(Qt4Project *project)
    : AbstractProcessStep(project), m_pro(project), m_forced(false)
{
}

QMakeStep::~QMakeStep()
{
}

QStringList QMakeStep::arguments(const QString &buildConfiguration)
{
    QStringList additonalArguments = value(buildConfiguration, "qmakeArgs").toStringList();
    QStringList arguments;
    arguments << project()->file()->fileName();
    if (!additonalArguments.contains("-spec")) {
        arguments << "-spec" << m_pro->qtVersion(buildConfiguration)->mkspec();
    }

    arguments << "-r";

    if (project()->value(buildConfiguration, "buildConfiguration").isValid()) {
        QStringList configarguments;
        QtVersion::QmakeBuildConfig defaultBuildConfiguration = m_pro->qtVersion(buildConfiguration)->defaultBuildConfig();
        QtVersion::QmakeBuildConfig projectBuildConfiguration = QtVersion::QmakeBuildConfig(project()->value(buildConfiguration, "buildConfiguration").toInt());
        if ((defaultBuildConfiguration & QtVersion::BuildAll) && !(projectBuildConfiguration & QtVersion::BuildAll))
            configarguments << "CONFIG-=debug_and_release";
        if (!(defaultBuildConfiguration & QtVersion::BuildAll) && (projectBuildConfiguration & QtVersion::BuildAll))
            configarguments << "CONFIG+=debug_and_release";
        if ((defaultBuildConfiguration & QtVersion::DebugBuild) && !(projectBuildConfiguration & QtVersion::DebugBuild))
            configarguments << "CONFIG+=release";
        if (!(defaultBuildConfiguration & QtVersion::DebugBuild) && (projectBuildConfiguration & QtVersion::DebugBuild))
            configarguments << "CONFIG+=debug";
        if (!configarguments.isEmpty())
            arguments << configarguments;
    } else {
        qWarning()<< "The project should always have a qmake build configuration set";
    }

    if (!additonalArguments.isEmpty())
        arguments << additonalArguments;

    return arguments;
}

bool QMakeStep::init(const QString &name)
{
    m_buildConfiguration = name;
    const QtVersion *qtVersion = m_pro->qtVersion(name);


    if (!qtVersion->isValid()) {
#if defined(Q_WS_MAC)
        emit addToOutputWindow(tr("\n<font color=\"#ff0000\"><b>No valid Qt version set. Set one in Preferences </b></font>\n"));
#else
        emit addToOutputWindow(tr("\n<font color=\"#ff0000\"><b>No valid Qt version set. Set one in Tools/Options </b></font>\n"));
#endif
        return false;
    }

    QStringList args = arguments(name);
    QString workingDirectory = m_pro->buildDirectory(name);

    QString program = qtVersion->qmakeCommand();

    // Check wheter we need to run qmake
    bool needToRunQMake = true;
    if (QDir(workingDirectory).exists(QLatin1String("Makefile"))) {
        QString qtPath = QtVersionManager::findQtVersionFromMakefile(workingDirectory);
        if (qtVersion->path() == qtPath) {
            needToRunQMake = !m_pro->compareBuildConfigurationToImportFrom(name, workingDirectory);
        }
    }

    if (m_forced) {
        m_forced = false;
        needToRunQMake = true;
    }

    setEnabled(name, needToRunQMake);
    setWorkingDirectory(name, workingDirectory);
    setCommand(name, program);
    setArguments(name, args);
    setEnvironment(name, m_pro->environment(name));
    return AbstractProcessStep::init(name);
}

void QMakeStep::run(QFutureInterface<bool> &fi)
{
    if (qobject_cast<Qt4Project *>(project())->rootProjectNode()->projectType() == ScriptTemplate) {
        fi.reportResult(true);
        return;
    }
    
    if (!enabled(m_buildConfiguration)) {
        emit addToOutputWindow(tr("<font color=\"#0000ff\">Configuration unchanged, skipping QMake step.</font>"));
        fi.reportResult(true);
        return;
    }
    AbstractProcessStep::run(fi);
}

QString QMakeStep::name()
{
    return Constants::QMAKESTEP;
}

QString QMakeStep::displayName()
{
    return "QMake";
}

void QMakeStep::setForced(bool b)
{
    m_forced = b;
}

bool QMakeStep::forced()
{
    return m_forced;
}

ProjectExplorer::BuildStepConfigWidget *QMakeStep::createConfigWidget()
{
    return new QMakeStepConfigWidget(this);
}

bool QMakeStep::immutable() const
{
    return true;
}


void QMakeStep::processStartupFailed()
{
    m_forced = true;
    AbstractProcessStep::processStartupFailed();
}

bool QMakeStep::processFinished(int exitCode, QProcess::ExitStatus status)
{
    bool result = AbstractProcessStep::processFinished(exitCode, status);
    if (!result)
        m_forced = true;
    return result;
}

void QMakeStep::setQMakeArguments(const QString &buildConfiguration, const QStringList &arguments)
{
    setValue(buildConfiguration, "qmakeArgs", arguments);
    emit changed();
}

QMakeStepConfigWidget::QMakeStepConfigWidget(QMakeStep *step)
    : BuildStepConfigWidget(), m_step(step)
{
    m_ui.setupUi(this);
    connect(m_ui.qmakeAdditonalArgumentsLineEdit, SIGNAL(textEdited(const QString&)),
            this, SLOT(qmakeArgumentsLineEditTextEdited()));
    connect(m_ui.buildConfigurationComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(buildConfigurationChanged()));
    connect(step, SIGNAL(changed()),
            this, SLOT(update()));
}

void QMakeStepConfigWidget::qmakeArgumentsLineEditTextEdited()
{
    Q_ASSERT(!m_buildConfiguration.isNull());
    m_step->setValue(m_buildConfiguration, "qmakeArgs", ProjectExplorer::Environment::parseCombinedArgString(m_ui.qmakeAdditonalArgumentsLineEdit->text()));
    m_ui.qmakeArgumentsEdit->setPlainText(ProjectExplorer::Environment::joinArgumentList(m_step->arguments(m_buildConfiguration)));
    static_cast<Qt4Project *>(m_step->project())->invalidateCachedTargetInformation();
}

void QMakeStepConfigWidget::buildConfigurationChanged()
{
    QtVersion::QmakeBuildConfig buildConfiguration = QtVersion::QmakeBuildConfig(m_step->project()->value(m_buildConfiguration, "buildConfiguration").toInt());
    if (m_ui.buildConfigurationComboBox->currentIndex() == 0) {
        // debug
        buildConfiguration = QtVersion::QmakeBuildConfig(buildConfiguration | QtVersion::DebugBuild);
    } else {
        buildConfiguration = QtVersion::QmakeBuildConfig(buildConfiguration & ~QtVersion::DebugBuild);
    }
    m_step->project()->setValue(m_buildConfiguration, "buildConfiguration", int(buildConfiguration));
    m_ui.qmakeArgumentsEdit->setPlainText(ProjectExplorer::Environment::joinArgumentList(m_step->arguments(m_buildConfiguration)));
    static_cast<Qt4Project *>(m_step->project())->invalidateCachedTargetInformation();
}

QString QMakeStepConfigWidget::displayName() const
{
    return m_step->displayName();
}

void QMakeStepConfigWidget::update()
{
    init(m_buildConfiguration);
}

void QMakeStepConfigWidget::init(const QString &buildConfiguration)
{
    m_buildConfiguration = buildConfiguration;
    QString qmakeArgs = ProjectExplorer::Environment::joinArgumentList(m_step->value(buildConfiguration, "qmakeArgs").toStringList());
    m_ui.qmakeAdditonalArgumentsLineEdit->setText(qmakeArgs);
    m_ui.qmakeArgumentsEdit->setPlainText(ProjectExplorer::Environment::joinArgumentList(m_step->arguments(buildConfiguration)));
    bool debug = QtVersion::QmakeBuildConfig(m_step->project()->value(buildConfiguration, "buildConfiguration").toInt()) & QtVersion::DebugBuild;
    m_ui.buildConfigurationComboBox->setCurrentIndex(debug? 0 : 1);
}

////
// QMakeStepFactory
////

QMakeStepFactory::QMakeStepFactory()
{
}

QMakeStepFactory::~QMakeStepFactory()
{
}

bool QMakeStepFactory::canCreate(const QString & name) const
{
    return (name == Constants::QMAKESTEP);
}

ProjectExplorer::BuildStep * QMakeStepFactory::create(ProjectExplorer::Project * pro, const QString & name) const
{
    Q_UNUSED(name)
    return new QMakeStep(static_cast<Qt4Project *>(pro));
}

QStringList QMakeStepFactory::canCreateForProject(ProjectExplorer::Project *pro) const
{
    Q_UNUSED(pro)
    return QStringList();
}

QString QMakeStepFactory::displayNameForName(const QString &name) const
{
    Q_UNUSED(name)
    return QString();
}


