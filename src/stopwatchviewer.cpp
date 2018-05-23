#include "stopwatchviewer.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <ctime>

StopwatchViewer::StopwatchViewer(QWidget * parent)
 : QWidget(parent)
{
	ui.setupUi(this);

	plotHolderWidget = new PlotHolderWidget();

	mySocket = new QUdpSocket(this);
	mySocket->bind(45454, QUdpSocket::ShareAddress);
    connect(mySocket, SIGNAL(readyRead()), this, SLOT(processPendingDatagram()));

	ui.tableWidget->setColumnCount(NUM_FIELDS);

	QStringList columnTitles;
	columnTitles << "Item" << "Last (ms)" << "Min (ms)" << "Max (ms)" << "Avg (ms)" << "Hz" << "Plot";

	assert(columnTitles.length() == NUM_FIELDS);

	ui.tableWidget->setHorizontalHeaderLabels(columnTitles);
	ui.tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
	ui.tableWidget->setColumnWidth(0, 180);

	columnTitles.removeFirst();
	columnTitles.removeLast();

	ui.plotChoiceComboBox->addItems(columnTitles);

    for(int i = 1; i < ui.tableWidget->columnCount(); i++)
    {
        ui.tableWidget->setColumnWidth(i, (ui.tableWidget->width() - 180) / (ui.tableWidget->columnCount() - 1) - (NUM_FIELDS - 1));
    }

    connect(ui.flushButton, SIGNAL(clicked()), this, SLOT(flushCache()));
    connect(ui.plotButton, SIGNAL(clicked()), this, SLOT(plotClicked()));

    lastRow = 0;
}

StopwatchViewer::~StopwatchViewer()
{
    mySocket->close();

    time_t now = time(0);
    std::string dt = ctime(&now);
    std::string filename = "../log_files/refreshRate--";
    filename += dt;
    filename += ".txt";

    std::ofstream file;
    file.open(filename.c_str(), std::fstream::out);
    std::stringstream strs;

    //Print all of the Run refresh rates to file
    for (std::list<float>::iterator it = runTimings.begin(); it != runTimings.end(); it++)
    {
      strs << *it << "\n";
    }

    file << strs.str();
    file.close();

    delete plotHolderWidget;

    delete mySocket;
}

void StopwatchViewer::resizeEvent(QResizeEvent *)
{
    for(int i = 1; i < ui.tableWidget->columnCount() - 1; i++)
    {
        ui.tableWidget->setColumnWidth(i, (ui.tableWidget->width() - 180 - ui.tableWidget->columnWidth(ui.tableWidget->columnCount() - 1)) / (ui.tableWidget->columnCount() - 2) - (NUM_FIELDS - 1));
    }
}

void StopwatchViewer::checkboxHit()
{
    int numChecked = 0;

    for(int i = 0; i < ui.tableWidget->rowCount(); i++)
    {
        QCheckBox * cellCheckBox = qobject_cast<QCheckBox *>(ui.tableWidget->cellWidget(i, (NUM_FIELDS - 1)));

        if(cellCheckBox->isChecked())
        {
            numChecked++;
        }

        if(numChecked == 4)
        {
            for(int j = 0; j < ui.tableWidget->rowCount(); j++)
            {
                QCheckBox * checkBox = qobject_cast<QCheckBox *>(ui.tableWidget->cellWidget(j, (NUM_FIELDS - 1)));

                if(!checkBox->isChecked())
                {
                    checkBox->setCheckable(false);
                }
            }
            break;
        }
    }

    if(numChecked < 4)
    {
        for(int j = 0; j < ui.tableWidget->rowCount(); j++)
        {
            QCheckBox * checkBox = qobject_cast<QCheckBox *>(ui.tableWidget->cellWidget(j, (NUM_FIELDS - 1)));
            checkBox->setCheckable(true);
        }
    }
}

void StopwatchViewer::processPendingDatagram()
{
    QByteArray datagram;
    datagram.resize(mySocket->pendingDatagramSize());
    mySocket->readDatagram(datagram.data(), datagram.size());

    int * data = (int *)datagram.data();

    if(datagram.size() > 0 &&
       datagram.size() == data[0])
    {
        std::pair<unsigned long long int, std::vector<std::pair<std::string, float> > > currentTimes = StopwatchDecoder::decodePacket((unsigned char *)datagram.data(), datagram.size());

        std::map<std::string, std::pair<RingBuffer<float, DEFAULT_RINGBUFFER_SIZE>, TableRow> > & stopwatch = cache[currentTimes.first];

        for(unsigned int i = 0; i < currentTimes.second.size(); i++)
        {
            stopwatch[currentTimes.second.at(i).first].first.add(currentTimes.second.at(i).second);
        }

        updateTable();
    }
}

void StopwatchViewer::updateTable()
{
    int currentNumTimers = 0;

    for(std::map<unsigned long long int, std::map<std::string, std::pair<RingBuffer<float, DEFAULT_RINGBUFFER_SIZE>, TableRow> > >::const_iterator it = cache.begin(); it != cache.end(); it++)
    {
        currentNumTimers += it->second.size();
    }

    ui.tableWidget->setRowCount(currentNumTimers);

    std::vector<float> plotVals;

    for(std::map<unsigned long long int, std::map<std::string, std::pair<RingBuffer<float, DEFAULT_RINGBUFFER_SIZE>, TableRow> > >::const_iterator it = cache.begin(); it != cache.end(); it++)
    {
        const std::map<std::string, std::pair<RingBuffer<float, DEFAULT_RINGBUFFER_SIZE>, TableRow> > & stopwatch = it->second;

        for(std::map<std::string, std::pair<RingBuffer<float, DEFAULT_RINGBUFFER_SIZE>, TableRow> >::const_iterator it = stopwatch.begin(); it != stopwatch.end(); it++)
        {
            TableRow & newEntry = const_cast<TableRow&>(it->second.second);
            if (it->first == "Run")
            {
              runTimings.push_back(it->second.first.getReciprocal() * 1000.0);
            }

            if(newEntry.isUninit())
            {
                newEntry.row = lastRow++;
                newEntry.tableItems = new QTableWidgetItem[NUM_FIELDS - 1];
                newEntry.tableItems[0].setText(QString::fromStdString(it->first));
                newEntry.tableItems[1].setText(QString::number(it->second.first[0]));
                newEntry.tableItems[2].setText(QString::number(it->second.first.getMinimum()));
                newEntry.tableItems[3].setText(QString::number(it->second.first.getMaximum()));
                newEntry.tableItems[4].setText(QString::number(it->second.first.getAverage()));
                newEntry.tableItems[5].setText(QString::number(it->second.first.getReciprocal() * 1000.0));

                newEntry.checkItem = new QCheckBox();
                connect(newEntry.checkItem, SIGNAL(stateChanged(int)), this, SLOT(checkboxHit()));

                ui.tableWidget->setItem(newEntry.row, 0, &newEntry.tableItems[0]);
                ui.tableWidget->setItem(newEntry.row, 1, &newEntry.tableItems[1]);
                ui.tableWidget->setItem(newEntry.row, 2, &newEntry.tableItems[2]);
                ui.tableWidget->setItem(newEntry.row, 3, &newEntry.tableItems[3]);
                ui.tableWidget->setItem(newEntry.row, 4, &newEntry.tableItems[4]);
                ui.tableWidget->setItem(newEntry.row, 5, &newEntry.tableItems[5]);
                ui.tableWidget->setCellWidget(newEntry.row, 6, newEntry.checkItem);
            }
            else
            {
                ui.tableWidget->item(newEntry.row, 0)->setText(QString::fromStdString(it->first));
                ui.tableWidget->item(newEntry.row, 1)->setText(QString::number(it->second.first[0]));
                ui.tableWidget->item(newEntry.row, 2)->setText(QString::number(it->second.first.getMinimum()));
                ui.tableWidget->item(newEntry.row, 3)->setText(QString::number(it->second.first.getMaximum()));
                ui.tableWidget->item(newEntry.row, 4)->setText(QString::number(it->second.first.getAverage()));
                ui.tableWidget->item(newEntry.row, 5)->setText(QString::number(it->second.first.getReciprocal() * 1000.0));
            }


            QCheckBox * cellCheckBox = qobject_cast<QCheckBox *>(ui.tableWidget->cellWidget(newEntry.row, 6));

            if(cellCheckBox->isChecked())
            {
                plotVals.push_back(ui.tableWidget->item(newEntry.row, ui.plotChoiceComboBox->currentIndex() + 1)->text().toDouble());
            }
        }
    }

    bool enabled[DataPlotWidget::NUM_PLOTS] = {plotVals.size() > 0 ? true : false,
                                               plotVals.size() > 1 ? true : false,
                                               plotVals.size() > 2 ? true : false,
                                               plotVals.size() > 3 ? true : false};

    float values[DataPlotWidget::NUM_PLOTS] = {0.0f, 0.0f, 0.0f, 0.0f};

    for(int i = 0; i < (int)plotVals.size() && i < DataPlotWidget::NUM_PLOTS; i++)
    {
        values[i] = plotVals[i];
    }

    plotHolderWidget->update(&values[0], &enabled[0]);
}

void StopwatchViewer::flushCache()
{
    cache.clear();
    lastRow = 0;
    updateTable();
}
