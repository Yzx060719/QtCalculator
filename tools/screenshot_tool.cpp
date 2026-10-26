/**
 * @file screenshot_tool.cpp
 * @brief 自动化截图工具：驱动程序走一遍实验要求的关键场景，并逐张保存界面截图。
 *
 * 使用方式（离屏渲染，无需人工干预）：
 *     QtCalculatorShots.exe -platform offscreen
 *
 * 工作原理：
 *   - 每个场景通过消息投递（发送 QKeyEvent / 直接触发按钮 click()）
 *     来模拟真实的键盘与鼠标输入，因此截图完全反映程序真实运行结果；
 *   - 每到一个关键状态调用 QWidget::grab() 把窗口渲染成 QPixmap 存盘；
 *   - 所有场景结束后打印每个场景的最终显示值，便于与预期结果核对。
 *
 * 注意：本工具仅用于实验报告截图与自测，不参与主程序功能。
 */

#include "calculator.h"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

#include <cstdio>

namespace {

QString g_outputDir;

/**
 * 显式加载中文字体。
 * 离屏（offscreen）平台不带字体数据库，直接渲染会出现方块乱码（tofu），
 * 因此这里手动把 Windows 的中文字体注册进 Qt，保证截图中文字正常显示。
 */
void loadChineseFont(QApplication &app)
{
    const QStringList candidates = {
        QStringLiteral("C:/Windows/Fonts/msyh.ttc"),    // 微软雅黑
        QStringLiteral("C:/Windows/Fonts/simhei.ttf"),  // 黑体
        QStringLiteral("C:/Windows/Fonts/simsun.ttc"),  // 宋体
        QStringLiteral("C:/Windows/Fonts/Deng.ttf"),    // 等线
    };
    for (const QString &path : candidates) {
        if (QFontDatabase::addApplicationFont(path) >= 0) {
            printf("[font] loaded %s\n", qPrintable(path));
            break;
        }
    }
    QFont font = app.font();
    font.setFamily(QStringLiteral("Microsoft YaHei"));
    font.setPointSize(10);
    app.setFont(font);
}

/// 按下键盘上的某个按键（发送到主窗口，触发 keyPressEvent）
void pressKey(QWidget *w, int key, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QKeyEvent press(QEvent::KeyPress, key, mods, QString());
    QApplication::sendEvent(w, &press);
    QApplication::processEvents();
}

/// 输入一个字符（用于数字、小数点、运算符这类以文本标识的按键）
void typeChar(QWidget *w, const QChar &ch)
{
    const int key = ch.isDigit() ? (Qt::Key_0 + ch.digitValue()) : ch.unicode();
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, QString(ch));
    QApplication::sendEvent(w, &press);
    QApplication::processEvents();
}

/// 输入一串字符
void typeText(QWidget *w, const QString &text)
{
    for (const QChar &ch : text) {
        typeChar(w, ch);
    }
}

/// 点击界面上的某个按钮（模拟鼠标点击）
void clickButton(QWidget *root, const QString &objectName)
{
    if (auto *btn = root->findChild<QPushButton *>(objectName)) {
        btn->click();
        QApplication::processEvents();
    } else {
        fprintf(stderr, "[warn] button not found: %s\n", qPrintable(objectName));
    }
}

/// 读取当前显示区文本
QString displayText(QWidget *root)
{
    if (auto *edit = root->findChild<QLineEdit *>("displayEdit")) {
        return edit->text();
    }
    return QStringLiteral("<no display>");
}

/// 读取当前算式前缀标签文本
QString expressionText(QWidget *root)
{
    if (auto *label = root->findChild<QLabel *>("expressionLabel")) {
        return label->text();
    }
    return QString();
}

/// 保存一张截图并打印状态
void capture(QWidget *w, const QString &fileName, const QString &title,
             const QString &note)
{
    const QString path = g_outputDir + QLatin1Char('/') + fileName;
    const QPixmap pixmap = w->grab();
    if (!pixmap.save(path)) {
        fprintf(stderr, "[error] failed to save %s\n", qPrintable(path));
        return;
    }
    printf("[shot] %-34s | display=%-14s expr=%-8s | %s\n",
           qPrintable(fileName), qPrintable(displayText(w)),
           qPrintable(expressionText(w)), qPrintable(note));
    fflush(stdout);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    loadChineseFont(app);

    g_outputDir = QDir::currentPath() + QStringLiteral("/shots");
    QDir().mkpath(g_outputDir);
    printf("[info] output dir = %s\n", qPrintable(g_outputDir));

    Calculator w;
    w.resize(360, 520);
    w.show();
    QApplication::processEvents();

    // 场景 1：初始界面
    capture(&w, QStringLiteral("01_initial.png"),
            QStringLiteral("初始界面"), QStringLiteral("默认显示 0，键盘可直接输入"));

    // 场景 2：鼠标输入小数运算 12.5 + 7.25 = 19.75
    clickButton(&w, QStringLiteral("digit1Button"));
    clickButton(&w, QStringLiteral("digit2Button"));
    clickButton(&w, QStringLiteral("decimalButton"));
    clickButton(&w, QStringLiteral("digit5Button"));
    clickButton(&w, QStringLiteral("plusButton"));
    capture(&w, QStringLiteral("02_mouse_decimal_first_operand.png"),
            QStringLiteral("鼠标-第一操作数"), QStringLiteral("12.5 + 已记录第一个操作数"));

    clickButton(&w, QStringLiteral("digit7Button"));
    clickButton(&w, QStringLiteral("decimalButton"));
    clickButton(&w, QStringLiteral("digit2Button"));
    clickButton(&w, QStringLiteral("digit5Button"));
    capture(&w, QStringLiteral("03_mouse_decimal_second_operand.png"),
            QStringLiteral("鼠标-第二操作数"), QStringLiteral("输入 7.25，算式前缀显示 12.5 +"));

    clickButton(&w, QStringLiteral("equalsButton"));
    capture(&w, QStringLiteral("04_mouse_decimal_result.png"),
            QStringLiteral("鼠标-小数运算结果"), QStringLiteral("12.5 + 7.25 = 19.75"));

    // 场景 3：重复输入小数点（异常输入）
    clickButton(&w, QStringLiteral("clearButton"));
    clickButton(&w, QStringLiteral("digit3Button"));
    clickButton(&w, QStringLiteral("decimalButton"));
    clickButton(&w, QStringLiteral("decimalButton"));
    clickButton(&w, QStringLiteral("decimalButton"));
    clickButton(&w, QStringLiteral("digit1Button"));
    clickButton(&w, QStringLiteral("digit4Button"));
    capture(&w, QStringLiteral("05_repeated_decimal.png"),
            QStringLiteral("重复小数点（异常输入）"),
            QStringLiteral("连按 3 次小数点仍只保留一个：3.14"));

    // 场景 4：键盘输入 8 * 6 = 48
    clickButton(&w, QStringLiteral("clearButton"));
    pressKey(&w, Qt::Key_8);
    pressKey(&w, Qt::Key_Asterisk);
    pressKey(&w, Qt::Key_6);
    capture(&w, QStringLiteral("06_keyboard_input.png"),
            QStringLiteral("键盘输入"), QStringLiteral("键盘 8 * 6，与鼠标同一套逻辑"));

    pressKey(&w, Qt::Key_Return);
    capture(&w, QStringLiteral("07_keyboard_result.png"),
            QStringLiteral("键盘-计算结果"), QStringLiteral("回车求值得 48"));

    // 场景 5：退格
    clickButton(&w, QStringLiteral("clearButton"));
    typeText(&w, QStringLiteral("12345"));
    pressKey(&w, Qt::Key_Backspace);
    pressKey(&w, Qt::Key_Backspace);
    capture(&w, QStringLiteral("08_backspace.png"),
            QStringLiteral("退格"), QStringLiteral("12345 退两格得 123"));

    // 场景 6：除零
    clickButton(&w, QStringLiteral("clearButton"));
    clickButton(&w, QStringLiteral("digit9Button"));
    clickButton(&w, QStringLiteral("divideButton"));
    clickButton(&w, QStringLiteral("digit0Button"));
    clickButton(&w, QStringLiteral("equalsButton"));
    capture(&w, QStringLiteral("09_divide_by_zero.png"),
            QStringLiteral("除数为 0（边界）"),
            QStringLiteral("提示「除数不能为 0」并变红"));

    // 场景 7：连续运算 2 + 3 + 4 = 9
    clickButton(&w, QStringLiteral("clearButton"));
    typeText(&w, QStringLiteral("2+3+"));
    capture(&w, QStringLiteral("10_chained_first_step.png"),
            QStringLiteral("连续运算-中间态"),
            QStringLiteral("2 + 3 + 已自动结算出 5"));
    typeText(&w, QStringLiteral("4="));
    capture(&w, QStringLiteral("11_chained_result.png"),
            QStringLiteral("连续运算-结果"), QStringLiteral("(2+3)+4 = 9"));

    // 场景 8：连续操作符（异常输入）
    clickButton(&w, QStringLiteral("clearButton"));
    typeText(&w, QStringLiteral("5+"));
    pressKey(&w, Qt::Key_Asterisk);
    capture(&w, QStringLiteral("12_repeated_operator.png"),
            QStringLiteral("连续操作符（异常）"),
            QStringLiteral("5 + × 只保留最后的 ×"));

    // 场景 9：结果后继续输入
    clickButton(&w, QStringLiteral("clearButton"));
    typeText(&w, QStringLiteral("6*7="));
    capture(&w, QStringLiteral("13_result_shown.png"),
            QStringLiteral("得出结果"), QStringLiteral("6 × 7 = 42"));
    typeText(&w, QStringLiteral("5"));
    capture(&w, QStringLiteral("14_after_result_input.png"),
            QStringLiteral("结果后继续输入"),
            QStringLiteral("结果后再按 5 应从新数字开始，得 5"));

    // 场景 10：清除后重新计算
    clickButton(&w, QStringLiteral("clearButton"));
    capture(&w, QStringLiteral("15_after_clear.png"),
            QStringLiteral("清除后"), QStringLiteral("C 键复位为 0，可重新开始"));

    typeText(&w, QStringLiteral("9-4="));
    capture(&w, QStringLiteral("16_recompute_after_clear.png"),
            QStringLiteral("清除后重算"), QStringLiteral("清除后 9 - 4 = 5"));

    printf("[done] 共输出截图到 %s\n", qPrintable(g_outputDir));
    return 0;
}
