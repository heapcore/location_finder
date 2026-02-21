#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCompleter>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMessageBox>
#include <QMetaType>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardItemModel>
#include <QTextStream>
#include <QTimer>
#include <algorithm>

SearchThread::SearchThread(
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
)
    : filePath_(filePath)
    , totalLinesCount_(totalLinesCount)
    , lagValue_(lagValue)
    , centerX_(centerX)
    , centerY_(centerY)
    , centerZ_(centerZ)
    , relativeLocationColumn_(relativeLocationColumn)
    , xColumn_(xColumn)
    , yColumn_(yColumn)
    , zColumn_(zColumn)
{
}

void SearchThread::requestStop()
{
    QMutexLocker locker(&mutex_);
    stopFlag_ = true;
}

bool SearchThread::shouldStop()
{
    QMutexLocker locker(&mutex_);
    return stopFlag_;
}

bool SearchThread::parseCoordinatesFromRow(const QStringList& fields, double& valueX, double& valueY, double& valueZ) const
{
    if (relativeLocationColumn_ >= 0) {
        if (relativeLocationColumn_ >= fields.size()) {
            return false;
        }

        QStringList parts = fields.at(relativeLocationColumn_)
            .trimmed()
            .split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() != 3) {
            return false;
        }

        bool okX = false;
        bool okY = false;
        bool okZ = false;
        valueX = parts.at(0).toDouble(&okX);
        valueY = parts.at(1).toDouble(&okY);
        valueZ = parts.at(2).toDouble(&okZ);
        return okX && okY && okZ;
    }

    if (xColumn_ < 0 || yColumn_ < 0 || zColumn_ < 0) {
        return false;
    }
    if (xColumn_ >= fields.size() || yColumn_ >= fields.size() || zColumn_ >= fields.size()) {
        return false;
    }

    bool okX = false;
    bool okY = false;
    bool okZ = false;
    valueX = fields.at(xColumn_).trimmed().toDouble(&okX);
    valueY = fields.at(yColumn_).trimmed().toDouble(&okY);
    valueZ = fields.at(zColumn_).trimmed().toDouble(&okZ);
    return okX && okY && okZ;
}

void SearchThread::run()
{
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorOccurred("Could not open CSV file.");
        return;
    }

    QTextStream in(&file);
    const QString headerLine = in.readLine();
    if (headerLine.isNull()) {
        emit errorOccurred("CSV file is empty.");
        return;
    }

    const QStringList headers = headerLine.split(',');
    std::vector<QStringList> searchResult;
    searchResult.push_back(headers);

    int linesProcessed = 0;
    int foundCount = 0;
    const int maxLines = std::max(1, totalLinesCount_);

    while (!in.atEnd()) {
        if (shouldStop()) {
            return;
        }

        const QString line = in.readLine();
        QStringList fields = line.split(',');
        linesProcessed++;

        if (fields.size() == headers.size()) {
            double valueX = 0.0;
            double valueY = 0.0;
            double valueZ = 0.0;

            if (parseCoordinatesFromRow(fields, valueX, valueY, valueZ)) {
                if (valueX >= (centerX_ - lagValue_) && valueX <= (centerX_ + lagValue_) &&
                    valueY >= (centerY_ - lagValue_) && valueY <= (centerY_ + lagValue_) &&
                    valueZ >= (centerZ_ - lagValue_) && valueZ <= (centerZ_ + lagValue_)) {
                    searchResult.push_back(fields);
                    foundCount++;
                    if (foundCount >= 100) {
                        emit progressChanged(100);
                        break;
                    }
                }
            }
        }

        const int progress = std::min(100, (linesProcessed * 100) / maxLines);
        emit progressChanged(progress);
    }

    emit dataReady(searchResult);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    model = new QStandardItemModel(this);

    lagValue = 100;
    totalLinesCount = 0;
    ui->lagEdit->setText(QString::number(lagValue));

    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::openFile);
    connect(ui->nearEdit, &QLineEdit::returnPressed, this, &MainWindow::searchData);
    connect(this, &MainWindow::searchFinished, this, &MainWindow::onSearchFinished);

    qRegisterMetaType<QString>("QString");
    qRegisterMetaType<std::vector<QStringList>>("std::vector<QStringList>");

    QSettings settings("myapp.ini", QSettings::IniFormat);
    QStringList history = settings.value("history/searches").toStringList();

    completer = new QCompleter(this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setCompletionMode(QCompleter::PopupCompletion);

    historyModel = new QStringListModel(history, completer);
    completer->setModel(historyModel);
    ui->nearEdit->setCompleter(completer);

    connect(this, &MainWindow::historyChanged, this, [this](const QStringList& historyList) {
        historyModel->setStringList(historyList);
    });
}

MainWindow::~MainWindow()
{
    if (searchThread) {
        searchThread->requestStop();
        searchThread->wait();
        delete searchThread;
        searchThread = nullptr;
    }

    if (currentSearchModel) {
        ui->tableView->setModel(nullptr);
        delete currentSearchModel;
        currentSearchModel = nullptr;
    }

    delete ui;
}

void MainWindow::openFile()
{
    QSettings settings("myapp.ini", QSettings::IniFormat);
    const QString lastPathKey = "last_opened_directory";
    QString lastPath = settings.value(lastPathKey).toString();

    if (lastPath.isEmpty()) {
        lastPath = QDir::homePath();
    }

    fileName = QFileDialog::getOpenFileName(this, "Open CSV File", lastPath, "CSV Files (*.csv)");

    if (!fileName.isEmpty()) {
        loadCsvData(fileName);

        QFileInfo fileInfo(fileName);
        settings.setValue(lastPathKey, fileInfo.absolutePath());
    }
}

bool MainWindow::resolveCoordinateColumns(const QStringList& headers, bool showErrors)
{
    relativeLocationColumn = -1;
    xColumn = -1;
    yColumn = -1;
    zColumn = -1;

    for (int i = 0; i < headers.size(); ++i) {
        const QString header = headers.at(i).trimmed().toLower();
        if (header == "relativelocation" || header == "relative_location") {
            relativeLocationColumn = i;
        } else if (header == "x") {
            xColumn = i;
        } else if (header == "y") {
            yColumn = i;
        } else if (header == "z") {
            zColumn = i;
        }
    }

    if (relativeLocationColumn >= 0) {
        return true;
    }

    if (xColumn >= 0 && yColumn >= 0 && zColumn >= 0) {
        return true;
    }

    if (showErrors) {
        QMessageBox::warning(
            this,
            "Warning",
            "CSV must contain either RelativeLocation with 'x y z' values or separate X, Y, Z columns."
        );
    }
    return false;
}

void MainWindow::loadCsvData(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Error", "Could not open file.");
        fileName.clear();
        return;
    }

    QTextStream in(&file);
    const QString headerLine = in.readLine();
    if (headerLine.isNull()) {
        QMessageBox::warning(this, "Warning", "CSV file is empty.");
        fileName.clear();
        return;
    }

    const QStringList headers = headerLine.split(',');
    if (!resolveCoordinateColumns(headers, true)) {
        fileName.clear();
        return;
    }

    model->clear();
    model->setHorizontalHeaderLabels(headers);

    int row = 0;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const QStringList fields = line.split(',');
        if (row < 100 && fields.size() == headers.size()) {
            for (int col = 0; col < fields.size(); ++col) {
                model->setItem(row, col, new QStandardItem(fields.at(col)));
            }
        }
        row++;
    }

    totalLinesCount = row;
    ui->tableView->setModel(model);
    adjustColumnWidths();
    ui->progressBar->setValue(0);
}

void MainWindow::searchData()
{
    if (searchInProgress) {
        return;
    }

    if (fileName.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Open a CSV file before searching.");
        return;
    }

    double centerX = 0.0;
    double centerY = 0.0;
    double centerZ = 0.0;
    const QString query = ui->nearEdit->text();
    if (!parseAndValidateQuery(query, centerX, centerY, centerZ, true)) {
        return;
    }

    bool ok = false;
    const int parsedLag = ui->lagEdit->text().toInt(&ok);
    if (!ok || parsedLag < 0) {
        QMessageBox::warning(this, "Warning", "Lag must be a non-negative integer.");
        ui->lagEdit->setText(QString::number(lagValue));
        return;
    }
    lagValue = parsedLag;

    if (searchThread) {
        searchThread->requestStop();
        searchThread->wait();
        delete searchThread;
        searchThread = nullptr;
    }

    searchInProgress = true;
    ui->nearEdit->setEnabled(false);
    ui->lagEdit->setEnabled(false);
    ui->progressBar->setValue(0);

    searchThread = new SearchThread(
        fileName,
        totalLinesCount,
        lagValue,
        centerX,
        centerY,
        centerZ,
        relativeLocationColumn,
        xColumn,
        yColumn,
        zColumn
    );

    connect(searchThread, &SearchThread::progressChanged, this, &MainWindow::setProgress);
    connect(searchThread, &SearchThread::dataReady, this, &MainWindow::handleDataReady);
    connect(searchThread, &SearchThread::errorOccurred, this, [this](const QString& message) {
        QMessageBox::warning(this, "Warning", message);
        emit searchFinished(false);
    });
    connect(searchThread, &QThread::finished, this, [this]() {
        searchInProgress = false;
        ui->nearEdit->setEnabled(true);
        ui->lagEdit->setEnabled(true);
        ui->progressBar->setValue(100);
    });

    searchThread->start();
}

void MainWindow::handleDataReady(const std::vector<QStringList>& data)
{
    if (currentSearchModel) {
        ui->tableView->setModel(nullptr);
        delete currentSearchModel;
        currentSearchModel = nullptr;
    }

    currentSearchModel = new QStandardItemModel(this);
    if (!data.empty()) {
        currentSearchModel->setHorizontalHeaderLabels(data.front());

        for (size_t row = 1; row < data.size(); ++row) {
            const QStringList& fields = data.at(row);
            for (int col = 0; col < fields.size(); ++col) {
                currentSearchModel->setItem(static_cast<int>(row - 1), col, new QStandardItem(fields.at(col)));
            }
        }
    }

    ui->tableView->setModel(currentSearchModel);
    QTimer::singleShot(0, this, &MainWindow::adjustColumnWidths);

    const bool found = data.size() > 1;
    emit searchFinished(found);
}

void MainWindow::adjustColumnWidths()
{
    QStandardItemModel* activeModel = qobject_cast<QStandardItemModel*>(ui->tableView->model());
    if (!activeModel) {
        return;
    }

    for (int column = 0; column < activeModel->columnCount(); ++column) {
        ui->tableView->resizeColumnToContents(column);
        int width = ui->tableView->columnWidth(column);
        const int maxWidth = 80 * fontMetrics().averageCharWidth();
        if (width > maxWidth) {
            ui->tableView->setColumnWidth(column, maxWidth);
        }
    }
}

void MainWindow::onSearchFinished(bool found)
{
    if (!found) {
        return;
    }

    const QString query = ui->nearEdit->text().trimmed();
    if (query.isEmpty()) {
        return;
    }

    QSettings settings("myapp.ini", QSettings::IniFormat);
    QStringList history = settings.value("history/searches").toStringList();
    if (!history.contains(query)) {
        history.prepend(query);
        if (history.size() > 20) {
            history.removeLast();
        }
        settings.setValue("history/searches", history);
        emit historyChanged(history);
    }
}

void MainWindow::setProgress(int value)
{
    ui->progressBar->setValue(value);
}

bool MainWindow::parseAndValidateQuery(const QString& query, double& valX, double& valY, double& valZ, bool showErrors)
{
    const QStringList parts = query.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (parts.size() != 3) {
        if (showErrors) {
            QMessageBox::warning(this, "Warning", "Invalid query: enter exactly three numbers (x y z).");
        }
        return false;
    }

    bool okX = false;
    bool okY = false;
    bool okZ = false;
    const double parsedX = parts.at(0).toDouble(&okX);
    const double parsedY = parts.at(1).toDouble(&okY);
    const double parsedZ = parts.at(2).toDouble(&okZ);

    if (!(okX && okY && okZ)) {
        if (showErrors) {
            QMessageBox::warning(this, "Warning", "Query values must be valid numbers.");
        }
        return false;
    }

    valX = parsedX;
    valY = parsedY;
    valZ = parsedZ;
    return true;
}
