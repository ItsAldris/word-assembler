#include "mainwindow.h"
#include "./ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // here add connect ui obj to fun. example:
    // connect(ui->connectBtn, &QPushButton::clicked, this, &MainWindow::connectBtnHit);

    connect(ui->connectButton, &QPushButton::clicked, this, &MainWindow::connectBtnHit);
    connect(ui->addressLineEdit, &QLineEdit::returnPressed, this, &MainWindow::connectBtnHit);

    connect(ui->acceptButton, &QPushButton::clicked, this, &MainWindow::sendBtnHit);
    connect(ui->usernameLineEdit, &QLineEdit::returnPressed, this, &MainWindow::sendBtnHit);

    connect(ui->disconnectButton, &QPushButton::clicked, this, &MainWindow::disconnectBtnHit);

    connect(ui->acceptButton_1, &QPushButton::clicked, this, &MainWindow::submitBtnHit);
    connect(ui->wordLineEdit, &QLineEdit::returnPressed, this, &MainWindow::submitBtnHit);

//    ui->plainTextEdit->setEnabled(false);
    ui->usernameGroupBox->setEnabled(false);
    isConnected = false;
}

MainWindow::~MainWindow()
{
     if(sock)
        sock->close();
    delete ui;
}

// 1 strona

void MainWindow::connectBtnHit(){
    ui->plainTextEdit->appendPlainText("connecting...");
//    if(sock)
//        delete sock;
    sock = new QTcpSocket(this);
    connect(sock, &QTcpSocket::connected, this, &MainWindow::socketConnected);
    connect(sock, &QTcpSocket::disconnected, this, &MainWindow::socketDisconnected);
    connect(sock, &QTcpSocket::errorOccurred, this, &MainWindow::socketError);
    connect(sock, &QTcpSocket::readyRead, this, &MainWindow::socketReadable);

    sock->connectToHost(ui->addressLineEdit->text(), ui->portSpinBox->value());

}

void MainWindow::sendBtnHit(){

    //jesli nie polaczono, wywolaj connectBtnHit
    if(!isConnected){
        connectBtnHit();
        return;
    }
    auto txt = ui->usernameLineEdit->text().trimmed();
    if(txt.isEmpty())
        return;
    sock->write((txt+'\n').toUtf8());
    ui->plainTextEdit->appendPlainText("waiting for server response...");

    //TODO check if username is accepted

    if (isConnected)
        ui->stackedWidget->setCurrentIndex(1);
    ui->plainTextEdit_2->setPlainText("username accepted");
}

void MainWindow::disconnectBtnHit(){
    if(sock && isConnected)
        delete sock;
    ui->stackedWidget->setCurrentIndex(0);
}

//funkcje sieciowe - obsluga socketu

void MainWindow::socketConnected(){
    isConnected = true;
    ui->plainTextEdit->appendPlainText("connected successfully");
    ui->usernameGroupBox->setEnabled(true);
}
void MainWindow::socketDisconnected(){
    isConnected = false;
    ui->plainTextEdit->appendPlainText("disconnected");
    ui->usernameGroupBox->setEnabled(false);
    ui->stackedWidget->setCurrentIndex(0);
}
void MainWindow::socketError(QTcpSocket::SocketError err){
    if(err == QTcpSocket::RemoteHostClosedError)
        return;
//    QMessageBox::critical(this, "Error", sock->errorString());
    isConnected = false;
    ui->plainTextEdit->appendPlainText("Socket error: "+sock->errorString());
    ui->usernameGroupBox->setEnabled(false);
    ui->stackedWidget->setCurrentIndex(0);
}
void MainWindow::socketReadable(){
    QByteArray ba = sock->readAll();
    QString text = QString::fromUtf8(ba).trimmed();
    msgParser(text);
    ui->plainTextEdit_2->appendPlainText(QString::fromUtf8(ba).trimmed());
}

void MainWindow::msgParser(QString &text) {
    msgBuf.append(text);

    while (msgBuf.contains("{") && msgBuf.contains("}")){
        int startIdx = msgBuf.indexOf("{");
        int endIdx = msgBuf.indexOf("}");
        if (startIdx > endIdx) {
            msgBuf.remove(0, endIdx + 1);
            continue;
        }

        QString msg = msgBuf.mid(startIdx + 1, endIdx - startIdx - 1);
        QString args;
        int argsIdx = msg.indexOf(":");
        if (argsIdx != -1) {
            args = msg.mid(argsIdx + 1);
            msg = msg.left(argsIdx);
        }

        if (msg.startsWith("sx")) { // Server
            ui->plainTextEdit->appendPlainText("Server shut down");
        } else if (msg[0]=='p') { // Player
            if (msg[1] == 'n') {
                ui->plainTextEdit_2->appendPlainText("Not enough players to play!");
            } else if (msg[1] == 'c' || msg[1] == 'd' || msg[1] == 'a') {
                QString text = "Player " + args;
                if (msg[1] == 'c') text += " connected!";
                else if (msg[1] == 'd') text += " disconnected!";
                else if (msg[1] == 'a') text += " answered!";
                ui->plainTextEdit_2->appendPlainText(text);
            }
        } else if (msg[0]=='g') { // Game
            if (msg[1] == 'w') ui->lcdNumber->display(args); //Wait time
            else if (msg[1] == 'e') { /* Print best players */ } //End
        } else if (msg[0]=='r') { // Round
            if (msg[1] == 'w') ui->lcdNumber->display(args); //Wait time
            else if (msg[1] == 'e') { /* Update stats */ } //End
            else if (msg[1] == 's') { /* Update round number */ } //Start
        } else if (msg[0]=='l') { // Letters
            if (msg[1] == 'l') ui->lettersLineEdit->setText(args); //List
        } else {
            ui->statsPlainTextEdit->appendPlainText(msg);
        }

        // Remove processed message from buffer
        msgBuf.remove(0, endIdx + 1);
    }
}


// 2 strona

void MainWindow::submitBtnHit(){

    //jesli nie polaczono, wywolaj connectBtnHit
    if(!isConnected){
        connectBtnHit();
        return;
    }
    auto txt = ui->wordLineEdit->text().trimmed();
    if(txt.isEmpty())
        return;
    sock->write((txt+'\n').toUtf8());
    ui->plainTextEdit_2->appendPlainText("waiting for server response...");
    ui->wordLineEdit->clear();
}




