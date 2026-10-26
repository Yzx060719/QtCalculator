#include "calculator.h"
#include "ui_calculator.h"

#include <QKeyEvent>
#include <QLocale>
#include <QPushButton>
#include <QStyle>

#include <cmath>

// ---------------------------------------------------------------------------
// 集中定义界面用到的固定字符串，避免魔法值散落各处
// ---------------------------------------------------------------------------
const QString Calculator::kZeroText          = QStringLiteral("0");
const QString Calculator::kErrorDivideByZero = QStringLiteral("除数不能为 0");

namespace {
/// 允许输入的最大字符数，与 ui 中 displayEdit 的 maxLength 保持一致
constexpr int kMaxInputLength = 17;
/// 浮点比较误差容限
constexpr double kEpsilon = 1e-12;

/// 运算符在界面上的显示符号与内部计算符号之间的映射
QString displaySymbol(const QString &op)
{
    if (op == QStringLiteral("*")) return QStringLiteral("×");
    if (op == QStringLiteral("/")) return QStringLiteral("÷");
    if (op == QStringLiteral("-")) return QStringLiteral("−");
    return op;
}

/// displaySymbol 的逆向映射：把按钮上的显示符号翻译回内部运算符号
QString operatorFromDisplay(const QString &symbol)
{
    if (symbol == QStringLiteral("×")) return QStringLiteral("*");
    if (symbol == QStringLiteral("÷")) return QStringLiteral("/");
    if (symbol == QStringLiteral("−")) return QStringLiteral("-");
    return symbol;
}
} // namespace

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------
Calculator::Calculator(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::Calculator)
    , m_operand(0.0)
    , m_pendingOp()
    , m_isEnteringNumber(false)
    , m_hasResult(false)
{
    ui->setupUi(this);

    setStyleSheet(QStringLiteral(R"(
/* ---------- 整体 ---------- */
QMainWindow, #centralWidget {
    background-color: #f4f6f8;
}
/* ---------- 显示区 ---------- */
#displayFrame {
    background-color: #ffffff;
    border: 1px solid #dfe3e8;
    border-radius: 12px;
}
#expressionLabel {
    color: #8a95a5;
    font-size: 15px;
    border: none;
    background: transparent;
}
#displayEdit {
    background: transparent;
    border: none;
    color: #1f2b3a;
    font-size: 34px;
    font-weight: 600;
    padding: 0px;
}
#displayEdit[error="true"] {
    color: #d93a3a;
    font-size: 24px;
}
/* ---------- 按键通用样式 ---------- */
QPushButton {
    background-color: #ffffff;
    border: 1px solid #dfe3e8;
    border-radius: 10px;
    color: #1f2b3a;
    font-size: 20px;
}
QPushButton:hover  { background-color: #eef2f7; }
QPushButton:pressed{ background-color: #dde5ee; }
/* 数字键 */
QPushButton[btnRole="digit"] { font-size: 22px; font-weight: 600; }
/* 功能键（C / ± / % / ⌫） */
QPushButton[btnRole="function"] {
    background-color: #e9edf2;
    color: #46536a;
    font-size: 18px;
}
QPushButton[btnRole="function"]:hover   { background-color: #dfe5ec; }
QPushButton[btnRole="function"]:pressed { background-color: #d1d9e3; }
/* 运算符键 */
QPushButton[btnRole="operator"] {
    background-color: #eaf1ff;
    color: #1a63d8;
    font-size: 22px;
    font-weight: 600;
}
QPushButton[btnRole="operator"]:hover   { background-color: #dbe7ff; }
QPushButton[btnRole="operator"]:pressed { background-color: #c8daff; }
/* 等号键 */
QPushButton[btnRole="equals"] {
    background-color: #1a73e8;
    border-color: #1a73e8;
    color: #ffffff;
    font-size: 24px;
    font-weight: 700;
}
QPushButton[btnRole="equals"]:hover   { background-color: #1666cc; }
QPushButton[btnRole="equals"]:pressed { background-color: #1257ad; }
)"));

    // 用动态属性区分按键角色，样式表据此上色，避免为每个按钮单独写样式
    ui->clearButton->setProperty("btnRole", "function");
    ui->signButton->setProperty("btnRole", "function");
    ui->percentButton->setProperty("btnRole", "function");
    ui->backspaceButton->setProperty("btnRole", "function");

    ui->divideButton->setProperty("btnRole", "operator");
    ui->multiplyButton->setProperty("btnRole", "operator");
    ui->minusButton->setProperty("btnRole", "operator");
    ui->plusButton->setProperty("btnRole", "operator");
    ui->equalsButton->setProperty("btnRole", "equals");

    const QList<QPushButton *> digitButtons = {
        ui->digit0Button, ui->digit1Button, ui->digit2Button, ui->digit3Button,
        ui->digit4Button, ui->digit5Button, ui->digit6Button, ui->digit7Button,
        ui->digit8Button, ui->digit9Button
    };
    for (QPushButton *btn : digitButtons) {
        btn->setProperty("btnRole", "digit");
    }

    connectDigitButtons();

    connect(ui->decimalButton,   &QPushButton::clicked, this, &Calculator::onDecimalClicked);
    connect(ui->plusButton,      &QPushButton::clicked, this, &Calculator::onOperatorClicked);
    connect(ui->minusButton,     &QPushButton::clicked, this, &Calculator::onOperatorClicked);
    connect(ui->multiplyButton,  &QPushButton::clicked, this, &Calculator::onOperatorClicked);
    connect(ui->divideButton,    &QPushButton::clicked, this, &Calculator::onOperatorClicked);
    connect(ui->equalsButton,    &QPushButton::clicked, this, &Calculator::onEqualsClicked);
    connect(ui->backspaceButton, &QPushButton::clicked, this, &Calculator::onBackspaceClicked);
    connect(ui->clearButton,     &QPushButton::clicked, this, &Calculator::onClearClicked);
    connect(ui->signButton,      &QPushButton::clicked, this, &Calculator::onToggleSignClicked);
    connect(ui->percentButton,   &QPushButton::clicked, this, &Calculator::onPercentClicked);

    ui->displayEdit->setText(kZeroText);
    ui->displayEdit->setFocusPolicy(Qt::NoFocus);   // 避免 QLineEdit 抢走键盘事件
    setFocusPolicy(Qt::StrongFocus);                // 主窗口主动接收键盘事件
    refreshExpressionLabel();
}

Calculator::~Calculator()
{
    delete ui;
}

// ---------------------------------------------------------------------------
// 连接与分发
// ---------------------------------------------------------------------------

/**
 * 所有数字键共用同一个槽：通过 sender() 的 text() 取出数字，
 * 这样 10 个数字键只需要一份处理逻辑。
 */
void Calculator::connectDigitButtons()
{
    const QList<QPushButton *> digitButtons = {
        ui->digit0Button, ui->digit1Button, ui->digit2Button, ui->digit3Button,
        ui->digit4Button, ui->digit5Button, ui->digit6Button, ui->digit7Button,
        ui->digit8Button, ui->digit9Button
    };
    for (QPushButton *btn : digitButtons) {
        connect(btn, &QPushButton::clicked, this, &Calculator::onDigitClicked);
    }
}

void Calculator::onDigitClicked()
{
    if (auto *btn = qobject_cast<QPushButton *>(sender())) {
        appendDigit(btn->text());
    }
}

/// 小数点按键：直接复用 appendDecimalPoint 的去重逻辑
void Calculator::onDecimalClicked()
{
    appendDecimalPoint();
}

/**
 * 运算符按键（+ − × ÷ 共用一个槽）。
 * 按钮上的显示符号（×、÷、−）需要翻译成内部运算符号后再交给 applyOperator，
 * 这样鼠标输入与键盘输入最终进入完全相同的处理路径。
 */
void Calculator::onOperatorClicked()
{
    auto *btn = qobject_cast<QPushButton *>(sender());
    if (!btn) {
        return;
    }
    applyOperator(operatorFromDisplay(btn->text()));
}

/**
 * 键盘事件的唯一入口。
 * 这里只负责把按键「翻译」成与鼠标一致的动作，具体逻辑全部复用上面的槽函数，
 * 从而保证鼠标输入与键盘输入行为完全一致。
 */
void Calculator::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    const QString text = event->text();

    // 数字 0-9（小键盘数字同样落在 Qt::Key_0..Key_9 区间）
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        appendDigit(QString(QChar('0' + (key - Qt::Key_0))));
        return;
    }

    switch (key) {
    case Qt::Key_Plus:
        applyOperator(QStringLiteral("+"));
        return;

    case Qt::Key_Minus:
        applyOperator(QStringLiteral("-"));
        return;

    case Qt::Key_Asterisk:
    case Qt::Key_multiply:
        applyOperator(QStringLiteral("*"));
        return;

    case Qt::Key_Slash:
    case Qt::Key_division:
        applyOperator(QStringLiteral("/"));
        return;

    case Qt::Key_Period:
    case Qt::Key_Comma:          // 部分键盘布局把小数点放在小键盘逗号上
        appendDecimalPoint();
        return;

    case Qt::Key_Equal:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        evaluate();
        return;

    case Qt::Key_Backspace:
        onBackspaceClicked();
        return;

    case Qt::Key_Delete:
    case Qt::Key_Escape:
        onClearClicked();
        return;

    case Qt::Key_Percent:
        onPercentClicked();
        return;

    default:
        break;
    }

    // 兜底：按字符内容再判断一次（覆盖 Shift+= 得到 "+" 等组合键场景）
    if (text.size() == 1) {
        const QChar ch = text.at(0);
        if (ch.isDigit()) {
            appendDigit(text);
            return;
        }
        if (ch == QLatin1Char('.')) { appendDecimalPoint(); return; }
        if (ch == QLatin1Char('+')) { applyOperator(QStringLiteral("+")); return; }
        if (ch == QLatin1Char('-')) { applyOperator(QStringLiteral("-")); return; }
        if (ch == QLatin1Char('*') || ch == QLatin1Char('x')) {
            applyOperator(QStringLiteral("*"));
            return;
        }
        if (ch == QLatin1Char('/')) { applyOperator(QStringLiteral("/")); return; }
        if (ch == QLatin1Char('=')) { evaluate(); return; }
        if (ch == QLatin1Char('%')) { onPercentClicked(); return; }
    }

    // Ctrl/Alt 组合键不拦截，交给基类处理（例如 Ctrl+Q 退出）
    QMainWindow::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// 操作数输入
// ---------------------------------------------------------------------------

/**
 * 输入一个数字字符。
 *
 * 处理要点：
 *  - 若当前显示的是上一次运算结果，或不在「输入中」状态，则先清空再输入，
 *    即「计算完成后继续输入」时以新数字重新开始；
 *  - 若当前显示为唯一的 "0"，则直接用新数字替换，避免出现 "05" 这种前导零；
 *  - 限制最大长度，防止显示溢出。
 */
void Calculator::appendDigit(const QString &digit)
{
    prepareForNewOperand();
    m_isEnteringNumber = true;

    QString current = ui->displayEdit->text();
    if (current == kZeroText) {
        current.clear();
    }
    if (current.size() >= kMaxInputLength) {
        return;                  // 已达长度上限，忽略本次输入
    }

    current += digit;
    ui->displayEdit->setText(current);
}

/**
 * 输入小数点。
 *
 * 避免同一操作数重复输入小数点的关键：
 *  - 进入新的操作数时（prepareForNewOperand 已处理）显示被重置为 "0"，
 *    此时输入小数点补成 "0."；
 *  - 若当前操作数字符串中已经包含小数点，则直接忽略本次输入。
 * 这样无论连续点击多少次小数点，一个操作数内都只会保留一个小数点。
 */
void Calculator::appendDecimalPoint()
{
    prepareForNewOperand();
    m_isEnteringNumber = true;

    QString current = ui->displayEdit->text();
    if (current.contains(QLatin1Char('.'))) {
        return;                  // 已存在小数点，忽略重复输入
    }
    if (current.isEmpty()) {
        current = kZeroText;
    }
    if (current.size() >= kMaxInputLength) {
        return;
    }
    ui->displayEdit->setText(current + QLatin1Char('.'));
}

/**
 * 当显示区内容不应被继续追加时（结果状态 / 非输入状态），把它重置为 "0"。
 * 所有输入类动作都先调用它，从而统一「结果后继续输入」的行为。
 */
void Calculator::prepareForNewOperand()
{
    if (!m_isEnteringNumber || m_hasResult) {
        ui->displayEdit->setText(kZeroText);
        m_hasResult = false;
    }
}

// ---------------------------------------------------------------------------
// 退格 / 清除 / 正负号 / 百分号
// ---------------------------------------------------------------------------

/**
 * 退格：删除当前操作数的最后一个字符。
 * 若退到空串或只剩负号，则回退为 "0"；退格不影响待执行的运算符与第一个操作数。
 */
void Calculator::onBackspaceClicked()
{
    // 结果状态下退格：清空重新输入
    if (m_hasResult) {
        onClearClicked();
        return;
    }
    if (!m_isEnteringNumber) {
        return;                  // 尚未开始输入，不做处理
    }

    QString current = ui->displayEdit->text();
    current.chop(1);
    if (current.isEmpty() || current == QStringLiteral("-")) {
        current = kZeroText;
    }
    ui->displayEdit->setText(current);
}

/**
 * 清除：把显示区与全部运算状态一起恢复到初始状态（相当于按下 "C"）。
 * 清除后可以立即开始一次全新的计算。
 */
void Calculator::onClearClicked()
{
    resetState();
    ui->displayEdit->setText(kZeroText);
    ui->displayEdit->setProperty("error", false);
    refreshExpressionLabel();
    // 动态属性变化后需要重新应用样式表
    ui->displayEdit->style()->unpolish(ui->displayEdit);
    ui->displayEdit->style()->polish(ui->displayEdit);
}

/**
 * 正负号：切换当前操作数的符号。
 * 若当前不是输入态（例如刚显示结果），则对结果本身取反。
 */
void Calculator::onToggleSignClicked()
{
    if (m_hasResult) {
        m_isEnteringNumber = true;   // 允许对结果直接取反后再继续运算
    }
    const QString current = ui->displayEdit->text();
    if (current.isEmpty() || current == kZeroText) {
        return;
    }
    if (current.startsWith(QLatin1Char('-'))) {
        ui->displayEdit->setText(current.mid(1));
    } else {
        ui->displayEdit->setText(QStringLiteral("-") + current);
    }
}

/**
 * 百分号：把当前操作数除以 100。
 * 采用「就地转换」语义，与手机计算器一致。
 */
void Calculator::onPercentClicked()
{
    bool ok = false;
    const double value = QLocale::c().toDouble(ui->displayEdit->text(), &ok);
    if (!ok) {
        return;
    }
    ui->displayEdit->setText(formatNumber(value / 100.0));
    m_isEnteringNumber = true;
    m_hasResult = false;
}

// ---------------------------------------------------------------------------
// 二元运算与状态管理
// ---------------------------------------------------------------------------

/**
 * 记录一个运算符。
 *
 * 状态管理说明：
 *  - 第一个操作数 m_operand：在遇到第一个运算符时，把当前显示值存进去；
 *  - 待执行运算符 m_pendingOp：保存尚未参与计算的运算符；
 *  - 第二个操作数：即用户随后输入并显示在界面上的值。
 * 具体行为：
 *  1. 若已经存在待执行运算符且用户已经输入了第二个操作数，则先结算前一次运算
 *     （实现「连续运算」1+2+3 逐步求值）；
 *  2. 若用户连续按下运算符而没有输入新的数字，则只替换运算符
 *     （处理「连续操作符」异常输入，如 5 + × → 5 × ）。
 */
void Calculator::applyOperator(const QString &op)
{
    // 前一次运算出错时，先清除错误再重新开始
    if (ui->displayEdit->property("error").toBool()) {
        onClearClicked();
    }

    bool ok = false;
    const double currentValue = QLocale::c().toDouble(ui->displayEdit->text(), &ok);
    if (!ok) {
        return;
    }

    if (!m_pendingOp.isEmpty()) {
        if (m_isEnteringNumber) {
            // 已有第二个操作数 → 先结算前一次运算，把结果作为新的第一个操作数
            double result = 0.0;
            if (!compute(m_operand, currentValue, m_pendingOp, &result)) {
                return;          // 除零等情况已在 compute 内部给出提示
            }
            m_operand = result;
            ui->displayEdit->setText(formatNumber(result));
        } else {
            // 连续输入运算符，只替换掉旧的运算符
            m_pendingOp = op;
            refreshExpressionLabel();
            return;
        }
    } else {
        // 第一次遇到运算符，把当前显示值记为第一个操作数
        m_operand = currentValue;
    }

    m_pendingOp = op;
    m_isEnteringNumber = false;   // 等待输入第二个操作数
    m_hasResult = false;
    refreshExpressionLabel();
}

/**
 * 执行一次二目运算。
 * @return 成功返回 true；除数为 0 等非法情况返回 false（并已提示用户）。
 */
bool Calculator::compute(double left, double right, const QString &op, double *result)
{
    double value = 0.0;
    if (op == QStringLiteral("+")) {
        value = left + right;
    } else if (op == QStringLiteral("-")) {
        value = left - right;
    } else if (op == QStringLiteral("*")) {
        value = left * right;
    } else if (op == QStringLiteral("/")) {
        // 除零保护：分母绝对值小于容限即视为 0
        if (std::fabs(right) < kEpsilon) {
            showError(kErrorDivideByZero);
            return false;
        }
        value = left / right;
    } else {
        return false;            // 未知运算符，理论上不会发生
    }

    // 过滤掉 -0 之类的负零结果
    if (std::fabs(value) < kEpsilon) {
        value = 0.0;
    }
    *result = value;
    return true;
}

/**
 * 等号求值。
 * 已经处于「待输入第二个操作数」状态时按下等号，视为对自身运算
 * （例如 5 + = → 5 + 5 = 10），与常见计算器行为一致。
 */
void Calculator::onEqualsClicked()
{
    if (ui->displayEdit->property("error").toBool()) {
        onClearClicked();
        return;
    }
    evaluate();
}

void Calculator::evaluate()
{
    if (m_pendingOp.isEmpty()) {
        // 没有待执行的运算，等号相当于确认当前值，直接标记为结果
        m_hasResult = true;
        m_isEnteringNumber = false;
        return;
    }

    bool ok = false;
    double rightValue = QLocale::c().toDouble(ui->displayEdit->text(), &ok);
    if (!ok) {
        return;
    }
    // 尚未输入第二个操作数就按等号：用第一个操作数自身补位
    if (!m_isEnteringNumber) {
        rightValue = m_operand;
    }

    double result = 0.0;
    if (!compute(m_operand, rightValue, m_pendingOp, &result)) {
        return;                  // 出错提示已在 compute 内完成
    }

    ui->displayEdit->setText(formatNumber(result));
    m_hasResult = true;
    m_isEnteringNumber = false;
    m_pendingOp.clear();         // 运算结束，清空待执行运算符
    refreshExpressionLabel();
}

// ---------------------------------------------------------------------------
// 显示辅助
// ---------------------------------------------------------------------------

/**
 * 数字格式化。
 *  - 使用 15 位有效数字，兼顾精度与可读性；
 *  - 去掉尾随的 0 和小数点（如 2.50 → 2.5，3.0 → 3）；
 *  - 超出常规范围时自动使用科学计数法。
 */
QString Calculator::formatNumber(double value) const
{
    if (std::isnan(value) || std::isinf(value)) {
        return QStringLiteral("错误");
    }
    if (std::fabs(value) < kEpsilon) {
        return kZeroText;
    }

    const double absValue = std::fabs(value);
    if (absValue >= 1e16 || absValue < 1e-9) {
        return QString::number(value, 'g', 8);
    }

    QString text = QString::number(value, 'g', 15);
    // 去掉多余的尾随 0，让 2.50 显示为 2.5
    if (!text.contains(QLatin1Char('e')) && text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0'))) {
            text.chop(1);
        }
        if (text.endsWith(QLatin1Char('.'))) {
            text.chop(1);
        }
    }
    return text;
}

/// 刷新算式前缀标签，例如显示 "12 + "，让用户看到运算上下文
void Calculator::refreshExpressionLabel()
{
    if (m_pendingOp.isEmpty()) {
        ui->expressionLabel->clear();
    } else {
        ui->expressionLabel->setText(formatNumber(m_operand)
                                     + QLatin1Char(' ')
                                     + displaySymbol(m_pendingOp));
    }
}

/// 统一的错误提示：显示错误文字并给显示区打上 error 属性以便样式表变红
void Calculator::showError(const QString &message)
{
    ui->displayEdit->setText(message);
    ui->displayEdit->setProperty("error", true);
    ui->displayEdit->style()->unpolish(ui->displayEdit);
    ui->displayEdit->style()->polish(ui->displayEdit);

    // 出错后清空运算状态，等用户清除或输入新数字时重新开始
    m_operand = 0.0;
    m_pendingOp.clear();
    m_isEnteringNumber = false;
    m_hasResult = true;
    ui->expressionLabel->clear();
}

/// 复位全部运算状态（不清除显示内容，由调用方决定如何显示）
void Calculator::resetState()
{
    m_operand = 0.0;
    m_pendingOp.clear();
    m_isEnteringNumber = false;
    m_hasResult = false;
}
