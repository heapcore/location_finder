#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QCompleter>
#include <QMutex>
#include <QThread>
#include <vector>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

Q_DECLARE_METATYPE(std::vector<QStringList>)

class MainWindow; // Forward declaration for SearchThread

class SearchThread : public QThread {
    Q_OBJECT
public:
    SearchThread(
        const QString& filePath,
        int totalLinesCount,
        int lagValue,
        double centerX,
        double centerY,
        double centerZ,
        int relativeLocationColumn,
        int xColumn,
        int yColumn,
        int zColumn
    );
    void requestStop();
    void run() override;

signals:
    void dataReady(const std::vector<QStringList>& data);
    void progressChanged(int value);
    void errorOccurred(const QString& message);

private:
    QString filePath_;
    int totalLinesCount_;
    int lagValue_;
    double centerX_;
    double centerY_;
    double centerZ_;
    int relativeLocationColumn_;
    int xColumn_;
    int yColumn_;
    int zColumn_;
    bool stopFlag_ = false;
    QMutex mutex_;
    bool shouldStop();
    bool parseCoordinatesFromRow(const QStringList& fields, double& valueX, double& valueY, double& valueZ) const;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

signals:
    void searchFinished(bool found);
    void historyChanged(QStringList history);

private slots:
    void openFile();
    bool parseAndValidateQuery(const QString& query, double& valX, double& valY, double& valZ, bool showErrors);
    void searchData();
    void onSearchFinished(bool found);
    void handleDataReady(const std::vector<QStringList>& data);
    void setProgress(int value);
    void loadCsvData(const QString& filePath);
    void adjustColumnWidths();

private:
    bool resolveCoordinateColumns(const QStringList& headers, bool showErrors);
    Ui::MainWindow *ui;
    QString fileName;
    QStandardItemModel *model;
    SearchThread *searchThread = nullptr;
    int lagValue;
    int totalLinesCount;
    int relativeLocationColumn = -1;
    int xColumn = -1;
    int yColumn = -1;
    int zColumn = -1;
    QCompleter* completer;
    QStringListModel* historyModel;
    bool searchInProgress = false;
    QStandardItemModel *currentSearchModel = nullptr;
};

#endif // MAINWINDOW_H
