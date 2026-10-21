#include <QApplication>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <tasks/Task.h>
#include <ui/dialogs/ProgressDialog.h>

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
        QObject::connect(&success, &Task::started, &success, [&success] {
            QTimer::singleShot(0, &success, [&success] { success.succeed(); });
        });
        // Use a timeout as a guard against a regression hanging the test runner.
        QTimer::singleShot(2000, &successDialog, &QDialog::reject);
        QCOMPARE(successDialog.execWithTask(&success), int(QDialog::Accepted));

        ControlledProgressTask aborted;
        ProgressDialog abortDialog;
        QObject::connect(&aborted, &Task::started, &aborted, [&aborted] {
            QTimer::singleShot(0, &aborted, [&aborted] { aborted.abort(); });
        });
        QTimer::singleShot(2000, &abortDialog, &QDialog::reject);
        QCOMPARE(abortDialog.execWithTask(&aborted), int(QDialog::Rejected));
        QVERIFY(aborted.isFinished());
    }
};

QTEST_MAIN(ProgressDialogTest)
#include "ProgressDialog_test.moc"
