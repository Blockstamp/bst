// Copyright (c) 2019 Michal Siek
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_MESSENGERPAGE_H
#define BITCOIN_QT_MESSENGERPAGE_H

#include <policy/feerate.h>
#include <qt/walletmodel.h>

#include <QWidget>
#include <univalue.h>

class WalletModel;
class ClientModel;
class QPlainTextEdit;
class PlatformStyle;
class QButtonGroup;
class CWalletTx;

namespace Ui {
    class MessengerPage;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

class MessengerPage : public QWidget
{
    Q_OBJECT

public:
    explicit MessengerPage(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~MessengerPage();

    void setClientModel(ClientModel *clientModel);
    void setModel(WalletModel *model);

private:
    Ui::MessengerPage *ui;
    WalletModel *walletModel;
    ClientModel *clientModel;
    std::string changeAddress;
    bool fFeeMinimized;
    CFeeRate feeRate;
    QButtonGroup *groupFee;
    const PlatformStyle *platformStyle;

    void unlockWallet();
    void minimizeFeeSection(bool fMinimize);
    void updateFeeMinimizedLabel();
    void updateCoinControlState(CCoinControl& ctrl);

    void fillUpTable(std::map<uint256, CWalletTx>& transactions);

    std::vector<unsigned char> getData();

protected:
    virtual void showEvent(QShowEvent * event);

public Q_SLOTS:
    void setBalance(const interfaces::WalletBalances& balances);
    void on_tabChanged();

private Q_SLOTS:

    void send();
    void read(const std::string& txnId);

    void on_buttonChooseFee_clicked();
    void on_buttonMinimizeFee_clicked();
    void setMinimumFee();
    void updateFeeSectionControls();
    void updateMinFeeLabel();
    void updateSmartFeeLabel();
    void updateDisplayUnit();
    void coinControlFeatureChanged(bool);
    void coinControlButtonClicked();
    void coinControlChangeChecked(int);
    void coinControlChangeEdited(const QString &);
    void coinControlUpdateLabels();
    void coinControlClipboardQuantity();
    void coinControlClipboardAmount();
    void coinControlClipboardFee();
    void coinControlClipboardAfterFee();
    void coinControlClipboardBytes();
    void coinControlClipboardLowOutput();
    void coinControlClipboardChange();

    void on_transactionsTableCellSelected(int row, int col);
};

#endif // BITCOIN_QT_MESSAGEPAGE_H
