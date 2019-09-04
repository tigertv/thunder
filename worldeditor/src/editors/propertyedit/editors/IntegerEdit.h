#ifndef INTEGEREDIT_H
#define INTEGEREDIT_H

#include <QWidget>

namespace Ui {
    class IntegerEdit;
}

class IntegerEdit : public QWidget {
    Q_OBJECT

public:
    explicit IntegerEdit (QWidget *parent = nullptr);
    ~IntegerEdit ();

    int32_t value () const;
    void setValue (int32_t value);

signals:
    void editingFinished ();

private:
    Ui::IntegerEdit *ui;
};

#endif // INTEGEREDIT_H
