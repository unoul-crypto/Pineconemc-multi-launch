#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <BuildConfig.h>
#include <tasks/Task.h>
#include <ui/dialogs/ProgressDialog.h>
#include <updater/PrismExternalUpdater.h>

class ControlledProgressTask final : public Task {
   public:
    int starts = 0;
    void executeTask() override { ++starts; }
    void succeed() { emitSucceeded(); }
    void fail() { emitFailed(QStringLiteral("Test failure")); }
    bool canAbort() const override { return true; }
    bool abort() override
    {
        emitAborted();
        return true;
    }
};

class ProgressDialogTest : public QObject {
    Q_OBJECT

   private slots:
    void updateCheckDoesNotBlockGuiOrStartTwice()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto settingsPath = directory.filePath("prismlauncher_update.cfg");
        {
            QSettings settings(settingsPath, QSettings::IniFormat);
            settings.setValue("auto_check", false);
        }
        auto program = BuildConfig.LAUNCHER_APP_BINARY_NAME + "_updater";
#ifdef Q_OS_WIN
        program += ".exe";
#else
        QVERIFY(QDir(directory.path()).mkpath("bin"));
        program = "bin/" + program;
#endif
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), directory.filePath(program)));
        QWidget parent;
        parent.show();
        PrismExternalUpdater updater(&parent, directory.path(), directory.path());
        QTimer heartbeat;
        QSignalSpy ticks(&heartbeat, &QTimer::timeout);
        heartbeat.start(15);
        QElapsedTimer elapsed;
        elapsed.start();
        updater.checkForUpdates(false);
        updater.checkForUpdates(false);
        QVERIFY(elapsed.elapsed() < 250);
        QTRY_VERIFY_WITH_TIMEOUT(!QSettings(settingsPath, QSettings::IniFormat).value("last_check").toString().isEmpty(), 5000);
        QVERIFY(ticks.count() >= 3);
        QVERIFY(parent.isEnabled());
        QVERIFY(QApplication::activeModalWidget() == nullptr);
        QFile starts(directory.filePath("probe-starts.txt"));
        QVERIFY(starts.open(QIODevice::ReadOnly));
        QCOMPARE(starts.readAll(), QByteArray("start\n"));
    }

    void missingUpdaterDoesNotOpenBackgroundModalDialog()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto settingsPath = directory.filePath("prismlauncher_update.cfg");
        {
            QSettings settings(settingsPath, QSettings::IniFormat);
            settings.setValue("auto_check", false);
        }
        QWidget parent;
        parent.show();
        PrismExternalUpdater updater(&parent, directory.path(), directory.path());
        updater.checkForUpdates(false);
        QTRY_VERIFY_WITH_TIMEOUT(!QSettings(settingsPath, QSettings::IniFormat).value("last_check").toString().isEmpty(), 5000);
        QVERIFY(parent.isEnabled());
        QVERIFY(QApplication::activeModalWidget() == nullptr);
    }

    void parallelProgressLeavesParentInteractive()
    {
        QWidget parent;
        QPushButton button(&parent);
        QSignalSpy clicks(&button, &QPushButton::clicked);
        parent.show();
        ControlledProgressTask first;
        ControlledProgressTask second;
        ProgressDialog firstDialog(&parent);
        ProgressDialog secondDialog(&parent);

        firstDialog.showWithTask(&first);
        secondDialog.showWithTask(&second);
        QTRY_COMPARE(first.starts, 1);
        QTRY_COMPARE(second.starts, 1);
        QVERIFY(parent.isEnabled());
        QVERIFY(!firstDialog.isModal());
        QVERIFY(!secondDialog.isModal());
        QVERIFY(QApplication::activeModalWidget() == nullptr);
        QTest::mouseClick(&button, Qt::LeftButton);
        QCOMPARE(clicks.count(), 1);

        first.succeed();
        QVERIFY(!firstDialog.isVisible());
        QVERIFY(secondDialog.isVisible());
        QVERIFY(second.isRunning());
        second.abort();
        QVERIFY(!secondDialog.isVisible());
        QVERIFY(parent.isEnabled());
    }

    void alreadyFinishedTaskDoesNotRestart()
    {
        ControlledProgressTask task;
        task.start();
        task.succeed();
        ProgressDialog dialog;
        dialog.showWithTask(&task);
        QCoreApplication::processEvents();
        QCOMPARE(task.starts, 1);
        QVERIFY(!dialog.isVisible());
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
    }

    void destroyedTaskClosesProgressWindow()
    {
        auto task = std::make_unique<ControlledProgressTask>();
        ProgressDialog dialog;
        task->start();
        dialog.showWithTask(task.get());
        QVERIFY(dialog.isVisible());
        task->abort();
        // Reuse the dialog with a running task, then destroy that task.
        task = std::make_unique<ControlledProgressTask>();
        task->start();
        dialog.showWithTask(task.get());
        task.reset();
        QVERIFY(!dialog.isVisible());
        QVERIFY(dialog.getTask() == nullptr);
    }

    void synchronousCompletionClosesBeforeGameRuns()
    {
        ControlledProgressTask task;
        task.start();
        QPointer<ProgressDialog> dialog = new ProgressDialog;
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->showWithTask(&task);
        task.succeed();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(dialog.isNull());
        QVERIFY(QApplication::activeModalWidget() == nullptr);
    }

    void modalCallersStillFinishAndAbort()
    {
        ControlledProgressTask success;
        ProgressDialog successDialog;
        // Task startup is queued by the dialog; finish after its started signal.
        QObject::connect(&success, &Task::started, &success,
                         [&success] { QTimer::singleShot(0, &success, [&success] { success.succeed(); }); });
        // Use a timeout as a guard against a regression hanging the test runner.
        QTimer::singleShot(2000, &successDialog, &QDialog::reject);
        QCOMPARE(successDialog.execWithTask(&success), int(QDialog::Accepted));

        ControlledProgressTask aborted;
        ProgressDialog abortDialog;
        QObject::connect(&aborted, &Task::started, &aborted,
                         [&aborted] { QTimer::singleShot(0, &aborted, [&aborted] { aborted.abort(); }); });
        QTimer::singleShot(2000, &abortDialog, &QDialog::reject);
        QCOMPARE(abortDialog.execWithTask(&aborted), int(QDialog::Rejected));
        QVERIFY(aborted.isFinished());
    }
};

int main(int argc, char** argv)
{
    // The test executable doubles as a deterministic, deliberately slow updater.
    for (int index = 1; index < argc; ++index) {
        if (QByteArray(argv[index]) == "--check-only") {
            QCoreApplication app(argc, argv);
            const auto arguments = app.arguments();
            const auto dirIndex = arguments.indexOf("--dir");
            if (dirIndex < 0 || dirIndex + 1 >= arguments.size())
                return 1;
            QFile starts(QDir(arguments.at(dirIndex + 1)).filePath("probe-starts.txt"));
            if (!starts.open(QIODevice::WriteOnly | QIODevice::Append))
                return 1;
            starts.write("start\n");
            starts.close();
            QThread::msleep(400);
            return 0;
        }
    }
    QApplication app(argc, argv);
    ProgressDialogTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ProgressDialog_test.moc"
