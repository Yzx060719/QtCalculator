#ifndef CALCULATOR_H
#define CALCULATOR_H

#include <QMainWindow>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui { class Calculator; }
QT_END_NAMESPACE

/**
 * @brief 带键盘事件的计算器主窗口
 *
 * 设计要点：
 *  1. 显示区使用一个只读 QLineEdit 展示「当前操作数」，另有一个 QLabel 展示
 *     已经确定下来的算式前缀（如 "12 + "），让用户随时看到运算上下文。
 *  2. 运算状态由四个成员变量集中管理：
 *       m_operand           —— 第一个操作数（左操作数）
 *       m_pendingOp         —— 待执行的运算符（无待执行运算时为空）
 *       m_isEnteringNumber  —— 是否正处于「输入操作数」状态
 *       m_hasResult         —— 当前显示的是否为一次运算的结果
 *  3. 所有按键（鼠标点击 / 键盘敲击）最终都汇聚到同一组槽函数，
 *     鼠标与键盘复用完全相同的处理逻辑，保证两种输入方式行为一致。
 */
class Calculator : public QMainWindow
{
    Q_OBJECT

public:
    explicit Calculator(QWidget *parent = nullptr);
    ~Calculator() override;

protected:
    /// 键盘事件入口：把按键翻译成与鼠标按钮一致的语义动作
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onDigitClicked();                       // 0-9 数字键
    void onDecimalClicked();                     // 小数点
    void onOperatorClicked();                    // + - * /
    void onEqualsClicked();                      // 等号
    void onBackspaceClicked();                   // 退格
    void onClearClicked();                       // 清除全部
    void onToggleSignClicked();                  // 正负号
    void onPercentClicked();                     // 百分号

private:
    /// 输入一个新数字字符（由所有数字键共用）
    void appendDigit(const QString &digit);
    /// 输入小数点（含重复小数点过滤）
    void appendDecimalPoint();
    /// 记录并显示运算符，处理连续运算符输入
    void applyOperator(const QString &op);
    /// 执行一次二目运算，返回是否成功（除零时返回 false）
    bool compute(double left, double right, const QString &op, double *result);
    /// 真正的等号求值逻辑
    void evaluate();
    /// 依据 m_hasResult / m_isEnteringNumber 决定下一段输入是否需要重置显示
    void prepareForNewOperand();

    /// 把 double 转成界面显示的字符串（去掉多余的 0，处理科学计数法与负零）
    QString formatNumber(double value) const;
    /// 刷新表达式前缀标签
    void refreshExpressionLabel();
    /// 统一处理错误提示（例如除数为 0）
    void showError(const QString &message);
    /// 清空内部运算状态，但不改界面显示
    void resetState();

    /// 统一给所有「操作数输入类」按钮绑定同一个 lambda，避免重复代码
    void connectDigitButtons();

    Ui::Calculator *ui;

    double  m_operand;           // 第一个操作数
    QString m_pendingOp;         // 待执行的运算符，空串表示没有
    bool    m_isEnteringNumber;  // 是否正在输入新的操作数
    bool    m_hasResult;         // 显示区当前是否是运算结果

    // 显示相关常量集中定义，避免魔法字符串散落
    static const QString kZeroText;
    static const QString kErrorDivideByZero;
};

#endif // CALCULATOR_H
