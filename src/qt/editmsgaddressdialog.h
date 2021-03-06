// Copyright (c) 2019 Michal Siek @ BioinfoBank Institute
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_EDITMSGADDRESSDIALOG_H
#define BITCOIN_QT_EDITMSGADDRESSDIALOG_H

#include <QDialog>

class MessengerBookModel;

namespace Ui {
    class EditMsgAddressDialog;
}

QT_BEGIN_NAMESPACE
class QDataWidgetMapper;
QT_END_NAMESPACE

/** Dialog for editing an address and associated information.
 */
class EditMsgAddressDialog : public QDialog
{
    Q_OBJECT

public:
    enum Mode {
        NewSendingAddress,
        EditSendingAddress
    };

    explicit EditMsgAddressDialog(Mode mode, QWidget *parent = 0);
    ~EditMsgAddressDialog();

    void setModel(MessengerBookModel *model);
    void loadRow(int row);

    QString getAddress() const;
    void initData(const std::string& label, const std::string& address);
public Q_SLOTS:
    void accept();

private Q_SLOTS:
    void validateRsaKey();

private:
    bool saveCurrentRow();

    /** Return a descriptive string when adding an already-existing address fails. */
    QString getDuplicateAddressWarning() const;

    Ui::EditMsgAddressDialog *ui;
    QDataWidgetMapper *mapper;
    Mode mode;
    MessengerBookModel *model;

    QString address;
};

#endif // BITCOIN_QT_EDITMSGADDRESSDIALOG_H
