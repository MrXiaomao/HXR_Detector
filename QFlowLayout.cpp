#include <QWidget>
#include "QFlowLayout.h"
#include <QDebug>

QFlowLayout::QFlowLayout(QWidget* parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing), m_scale(true)
{
    setContentsMargins(margin, margin, margin, margin);  // 边缘间距
}

QFlowLayout::QFlowLayout(int margin, int hSpacing, int vSpacing)
    : m_hSpace(hSpacing), m_vSpace(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

QFlowLayout::~QFlowLayout()
{
    QLayoutItem* item;
    // 内部所有内容析构
    while ((item = takeAt(0)))
        delete item;
}

void QFlowLayout::addItem(QLayoutItem* item)
{
    itemList.append(item);  // 将项目添加到布局
}

int QFlowLayout::horizontalSpacing() const
{
    // 小部件之间的间距
    if (m_hSpace >= 0) {
        return m_hSpace;
    }
    else {
        return smartSpacing(QStyle::PM_LayoutHorizontalSpacing);  // 在没有间隔的时候自动智能生成一段间隔
    }
}

int QFlowLayout::verticalSpacing() const
{
    if (m_vSpace >= 0) {
        return m_vSpace;
    }
    else {
        return smartSpacing(QStyle::PM_LayoutVerticalSpacing);
    }
}

int QFlowLayout::count() const
{
    return itemList.size(); // 返回布局中的项目数
}

QLayoutItem* QFlowLayout::itemAt(int index) const
{
    return itemList.value(index);
}

QLayoutItem* QFlowLayout::takeAt(int index)
{
    if (index >= 0 && index < itemList.size())
        return itemList.takeAt(index);  // 删除并返回项目, 如果某个项目被删除，其余的项目将被重新编号。
    return nullptr;
}

void QFlowLayout::setScale(bool scale/* = true*/)
{
    m_scale = scale;
}

Qt::Orientations QFlowLayout::expandingDirections() const
{
    return 0;
}

bool QFlowLayout::hasHeightForWidth() const
{
    return true;
}

int QFlowLayout::heightForWidth(int width) //const
{
    int height= doLayout(QRect(0, 0, width, 0), true);  // 高度取决于宽度的窗口小部件
    return height;
}

void QFlowLayout::setGeometry(const QRect& rect)
{
    // 计算布局项目的几何形状
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize QFlowLayout::sizeHint() const
{
    return minimumSize();
}

QSize QFlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem* item : qAsConst(itemList))
        size = size.expandedTo(item->minimumSize());

    const QMargins margins = contentsMargins();
    size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    return size;
}

int QFlowLayout::doLayout(const QRect& rect, bool testOnly) const
{
    int left, top, right, bottom;
    getContentsMargins(&left, &top, &right, &bottom);  // 计算布局项目可用的面积
    QRect effectiveRect = rect.adjusted(+left, +top, -right, -bottom);
    int x = effectiveRect.x();
    int y = effectiveRect.y();
    int lineHeight = 0;
    int height = verticalSpacing();

    int index = 1;
    for (QLayoutItem* item : qAsConst(itemList)) {
        const QWidget* wid = item->widget();
        int spaceX = horizontalSpacing();
        if (spaceX == -1)
            spaceX = wid->style()->layoutSpacing(
                QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Horizontal);
        int spaceY = verticalSpacing();
        if (spaceY == -1)
            spaceY = wid->style()->layoutSpacing(
                QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Vertical);

        int nextX = x + item->sizeHint().width() + spaceX;  // 下一个item的x坐标
        if (nextX - spaceX > effectiveRect.right() && lineHeight > 0) {  // 当前item的x坐标超过边界
            // 设置为下一行的第一个
            x = effectiveRect.x();
            y = y + lineHeight + spaceY;
            nextX = x + item->sizeHint().width() + spaceX;            
            height += lineHeight + spaceY;
            lineHeight = 0;
        }

        if (!testOnly)
            item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));

        x = nextX;
        lineHeight = qMax(lineHeight, item->sizeHint().height());
        index++;
    }

    if (m_scale){
        QObject* parent = this->parent();
        if (parent->isWidgetType()) {
            QWidget* pw = static_cast<QWidget*>(parent);
            pw->setFixedHeight(height + lineHeight);
        }
        //parentWidget()->setFixedHeight(height + lineHeight);
    }
    return y + lineHeight - rect.y() + bottom;
}

int QFlowLayout::smartSpacing(QStyle::PixelMetric pm) const
{
    // 当父是QWidget=>顶层布局的默认间距为pm样式。
    // 当父为QLayout=>子布局的默认间距由父布局的间距来确定。
    QObject* parent = this->parent();
    if (!parent) {
        return -1;
    }
    else if (parent->isWidgetType()) {
        QWidget* pw = static_cast<QWidget*>(parent);
        return pw->style()->pixelMetric(pm, nullptr, pw);
    }
    else {
        return static_cast<QLayout*>(parent)->spacing();
    }
}
