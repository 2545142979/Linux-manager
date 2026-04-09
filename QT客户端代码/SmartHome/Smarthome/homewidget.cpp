#include "homewidget.h"
#include "ui_homewidget.h"
#include <QMessageBox>
#include <QDateTime>
#include <QDebug>

namespace {

constexpr int kPacketSize = 36;

quint16 readBe16(const QByteArray &packet, int offset)
{
    return (static_cast<quint8>(packet[offset]) << 8)
        | static_cast<quint8>(packet[offset + 1]);
}

quint32 readLe32(const QByteArray &packet, int offset)
{
    return static_cast<quint8>(packet[offset])
        | (static_cast<quint32>(static_cast<quint8>(packet[offset + 1])) << 8)
        | (static_cast<quint32>(static_cast<quint8>(packet[offset + 2])) << 16)
        | (static_cast<quint32>(static_cast<quint8>(packet[offset + 3])) << 24);
}

bool isAsciiDigits(const QByteArray &data)
{
    for (char ch : data) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }
    return true;
}

} // namespace

HomeWidget::HomeWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::HomeWidget)
{
    ui->setupUi(this);
    setWindowTitle("智能家具控制系统 V1.0");

        // 日志初始化
        log("系统已启动");

        // 你的网络 + 图片接收逻辑
        socket = new QTcpSocket(this);
        connect(socket, &QTcpSocket::connected, this, &HomeWidget::netConnected);
        connect(socket, &QTcpSocket::disconnected, this, &HomeWidget::netDisConnected);
        connect(socket, &QTcpSocket::readyRead, this, &HomeWidget::recvMsg);
         connect(socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(netError(QAbstractSocket::SocketError)));

        time = new QTimer(this);
        connect(time, &QTimer::timeout, this, &HomeWidget::requestPic);

        envTimer = new QTimer(this);
        connect(envTimer, &QTimer::timeout, this, &HomeWidget::requestEnv);

        // 图片标签自适应
        ui->label_image->setScaledContents(true);
        ui->btn_monitor->setEnabled(false);
}

HomeWidget::~HomeWidget()
{
    delete ui;
}

// ===================== 灯 =====================
void HomeWidget::on_btn_light_clicked()
{
    lightState = !lightState;
       if(lightState){
           // 只改背景颜色，不清空图片
           ui->btn_light->setStyleSheet("background-color:yellow;");
           ui->btn_light->setText("灯 · 已开启");
           sendTextCommand("led_on", "发送开灯指令");
       }else{
           // 取消颜色，图片还在！
           ui->btn_light->setStyleSheet("background-color:none;");
           ui->btn_light->setText("灯 开关");
           sendTextCommand("led_off", "发送关灯指令");
       }
}

// ===================== 蜂鸣器 =====================
void HomeWidget::on_btn_buzzer_clicked()
{
    buzzerState = !buzzerState;
        if(buzzerState){
            ui->btn_buzzer->setStyleSheet("background-color:red;");
            ui->btn_buzzer->setText("蜂鸣器 · 响");
            sendTextCommand("beep_on", "发送开蜂鸣器指令");
        }else{
            ui->btn_buzzer->setStyleSheet("background-color:none;");
            ui->btn_buzzer->setText("蜂鸣器 开关");
            sendTextCommand("beep_off", "发送关蜂鸣器指令");
        }
}

// ===================== 风扇 =====================
void HomeWidget::on_btn_fan_clicked()
{
    fanState = !fanState;
        if(fanState){
            ui->btn_fan->setStyleSheet("background-color:#409EFF;");
            ui->btn_fan->setText("风扇 · 转动");
            sendTextCommand("fan_on", "发送开风扇指令");
        }else{
            ui->btn_fan->setStyleSheet("background-color:none;");
            ui->btn_fan->setText("风扇 开关");
            sendTextCommand("fan_off", "发送关风扇指令");
        }
}

// ===================== 连接服务器=====================
void HomeWidget::on_btn_connect_clicked()
{
    // 从输入框获取 IP 和 端口
        QString ip = ui->edit_ip->text().trimmed();
        QString portStr = ui->edit_port->text().trimmed();

        // 判空
        if (ip.isEmpty() || portStr.isEmpty()) {
            log("错误：IP 或 端口不能为空！");
            QMessageBox::warning(this, "提示", "IP 和端口不能为空！");
            return;
        }

        // 转成数字端口
        bool ok;
        quint16 port = portStr.toUInt(&ok);
        if (!ok || port < 1 || port > 65535) {
            log("错误：端口号无效！");
            QMessageBox::warning(this, "提示", "端口必须是 1~65535 的数字！");
            return;
        }

        // 如果已经连接，先断开
        if (socket->state() == QTcpSocket::ConnectedState) {
            socket->disconnectFromHost();
        }

        // 使用用户输入的 IP 和端口连接
        log("正在连接服务器：" + ip + ":" + portStr);
        socket->connectToHost(ip, port);
}
// ===================== 断开服务器 =====================
void HomeWidget::on_btn_disconnect_clicked()
{
    socket->disconnectFromHost();
    time->stop();
    envTimer->stop();
}

// ===================== 开始监控（获取图片） =====================
void HomeWidget::on_btn_monitor_clicked()
{
        if(!isMonitoring){
            time->start(100);
            isMonitoring = true;
            ui->btn_monitor->setStyleSheet("background-color:green");
            ui->btn_monitor->setText("停止监控");
            log("开始获取监控画面");
        }else{
            time->stop();
            isMonitoring = false;
            ui->btn_monitor->setStyleSheet("");
            ui->btn_monitor->setText("开始监控");
            log("停止获取监控画面");
        }
}

// ===================== 环境显示/隐藏 =====================
void HomeWidget::on_btn_env_clicked()
{
    envVisible = !envVisible;
        if(envVisible){
            ui->btn_env->setStyleSheet("background-color:lightgreen");
            ui->btn_env->setText("隐藏环境");

            // 显示所有环境控件
            ui->label_temp->show();
            ui->label_humi->show();
            ui->label_light->show();
            ui->progressBar->show();       // 光照进度条
            ui->progressBar_temp->show();   // 温度进度条
            ui->progressBar_humi->show();   // 湿度进度条
            log("显示环境信息");
        }else{
            ui->btn_env->setStyleSheet("");
            ui->btn_env->setText("显示环境");

            // 隐藏所有环境控件
            ui->label_temp->hide();
            ui->label_humi->hide();
            ui->label_light->hide();
            ui->progressBar->hide();       // 光照进度条
            ui->progressBar_temp->hide();   // 温度进度条
            ui->progressBar_humi->hide();   // 湿度进度条
            log("隐藏环境信息");
        }
}

void HomeWidget::netConnected()
{
    ui->btn_monitor->setEnabled(true);
       status = 1;
       pic_len = 0;
       recvBuffer.clear();
       envTimer->start(1000);
       requestEnv();
       log("服务器连接成功 ✅");
       QMessageBox::information(this,"成功","连接成功");
}

void HomeWidget::netDisConnected()
{
    ui->btn_monitor->setEnabled(false);
        time->stop();
        envTimer->stop();
        recvBuffer.clear();
        status = 1;
        pic_len = 0;
        isMonitoring = false;
        ui->btn_monitor->setStyleSheet("");
        ui->btn_monitor->setText("开始监控");
        log("已断开服务器 ❌");
}

void HomeWidget::recvMsg()
{
    recvBuffer.append(socket->readAll());
    processIncomingBuffer();
}

void HomeWidget::requestPic()
{
    if (socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    socket->write("pic\n");
}

void HomeWidget::requestEnv()
{
    if (socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    socket->write("env\n");
}

// ===================== 网络错误处理 =====================
void HomeWidget::netError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
       log("网络错误：" + socket->errorString());
       ui->btn_monitor->setEnabled(false);
       time->stop();
       envTimer->stop();
       QMessageBox::critical(this, "网络错误", socket->errorString());
}

// 日志输出
void HomeWidget::log(const QString &msg)
{
    QString t = QDateTime::currentDateTime().toString("HH:mm:ss");
        ui->textEdit_log->append("[" + t + "] " + msg);
}

void HomeWidget::processIncomingBuffer()
{
    while (true) {
        if (status == 0) {
            if (!tryConsumeImagePayload()) {
                return;
            }
            continue;
        }

        if (recvBuffer.isEmpty()) {
            return;
        }

        if (tryConsumeEnvPacket()) {
            continue;
        }

        if (tryConsumeImageLength()) {
            continue;
        }

        if (tryConsumeAckMessage()) {
            continue;
        }

        return;
    }
}

bool HomeWidget::tryConsumeAckMessage()
{
    const int newlineIndex = recvBuffer.indexOf('\n');
    if (newlineIndex < 0) {
        return false;
    }

    const QByteArray line = recvBuffer.left(newlineIndex).trimmed();
    recvBuffer.remove(0, newlineIndex + 1);

    if (line.isEmpty()) {
        return true;
    }

    log("服务器响应：" + QString::fromUtf8(line));
    return true;
}

bool HomeWidget::tryConsumeEnvPacket()
{
    if (recvBuffer.isEmpty() || static_cast<quint8>(recvBuffer.at(0)) != 0xBB) {
        return false;
    }

    if (recvBuffer.size() < kPacketSize) {
        return false;
    }

    const QByteArray packet = recvBuffer.left(kPacketSize);
    recvBuffer.remove(0, kPacketSize);
    applyEnvPacket(packet);
    return true;
}

bool HomeWidget::tryConsumeImageLength()
{
    if (recvBuffer.size() < 7) {
        return false;
    }

    const QByteArray lenData = recvBuffer.left(7);
    if (!isAsciiDigits(lenData)) {
        return false;
    }

    pic_len = lenData.toInt();
    recvBuffer.remove(0, 7);
    status = 0;
    log("图片长度：" + QString::number(pic_len) + " 字节");
    return true;
}

bool HomeWidget::tryConsumeImagePayload()
{
    if (recvBuffer.size() < pic_len) {
        return false;
    }

    const QByteArray imgData = recvBuffer.left(pic_len);
    recvBuffer.remove(0, pic_len);

    QPixmap pix;
    if (pix.loadFromData(imgData)) {
        ui->label_image->setPixmap(pix.scaled(ui->label_image->size(),
            Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        log("图片格式错误，已丢弃当前帧");
    }

    status = 1;
    pic_len = 0;
    return true;
}

void HomeWidget::applyEnvPacket(const QByteArray &packet)
{
    if (packet.size() < kPacketSize) {
        return;
    }

    const int temp = static_cast<int>(readBe16(packet, 4));
    const int humi = static_cast<int>(readBe16(packet, 6));
    const int light = static_cast<int>(readLe32(packet, 20));

    ui->label_temp->setText(QString("温度：%1 ℃").arg(temp));
    ui->label_humi->setText(QString("湿度：%1 %RH").arg(humi));
    ui->label_light->setText(QString("光照：%1 lx").arg(light));

    ui->progressBar_temp->setValue(temp);
    ui->progressBar_humi->setValue(humi);
    ui->progressBar->setValue(light > 1000 ? 100 : light / 10);

    log(QString("环境已更新：temp=%1 humi=%2 light=%3").arg(temp).arg(humi).arg(light));
}

void HomeWidget::sendTextCommand(const char *command, const QString &logMessage)
{
    if (socket->state() != QAbstractSocket::ConnectedState) {
        log("设备未连接，命令未发送");
        return;
    }

    socket->write(QByteArray(command) + '\n');
    log(logMessage);
}

