// Copyright (c) 2019 Michal Siek
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <algorithm>

#include <QMessageBox>
#include <QFileDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/coincontroldialog.h>
#include <qt/messengerpage.h>
#include <qt/forms/ui_messengerpage.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/walletmodel.h>
#include <qt/askpassphrasedialog.h>
#include <qt/storetxdialog.h>
#include <qt/sendcoinsdialog.h>

#include <chainparams.h>
#include <key_io.h>
#include <wallet/coincontrol.h>
#include <validation.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif
#include <net.h>
#include <utilmoneystr.h>
#include <consensus/validation.h>

#include <data/datautils.h>
#include <data/retrievedatatxs.h>

#include <messages/message_encryption.h>
#include <rpc/util.h>

#include <QSettings>
#include <QButtonGroup>
#include <array>
#include <vector>

static const std::array<int, 9> confTargets = { {2, 4, 6, 12, 24, 48, 144, 504, 1008} };
extern int getConfTargetForIndex(int index);
extern int getIndexForConfTarget(int target);

//TODO: 8=tag length, fix it with define
static constexpr int maxDataSize=MAX_OP_RETURN_RELAY-6-8;

MessengerPage::MessengerPage(const PlatformStyle *_platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MessengerPage),
    walletModel(0),
    clientModel(0),
    changeAddress(""),
    fFeeMinimized(true),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);

    GUIUtil::setupAddressWidget(ui->lineEditCoinControlChange, this);

    // Coin Control
    connect(ui->pushButtonCoinControl, &QPushButton::clicked, this, &MessengerPage::coinControlButtonClicked);
    connect(ui->checkBoxCoinControlChange, &QCheckBox::stateChanged, this, &MessengerPage::coinControlChangeChecked);
    connect(ui->lineEditCoinControlChange, &QValidatedLineEdit::textEdited, this, &MessengerPage::coinControlChangeEdited);

    // Coin Control: clipboard actions
    QAction *clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction *clipboardAmountAction = new QAction(tr("Copy amount"), this);
    QAction *clipboardFeeAction = new QAction(tr("Copy fee"), this);
    QAction *clipboardAfterFeeAction = new QAction(tr("Copy after fee"), this);
    QAction *clipboardBytesAction = new QAction(tr("Copy bytes"), this);
    QAction *clipboardLowOutputAction = new QAction(tr("Copy dust"), this);
    QAction *clipboardChangeAction = new QAction(tr("Copy change"), this);
    connect(clipboardQuantityAction, &QAction::triggered, this, &MessengerPage::coinControlClipboardQuantity);
    connect(clipboardAmountAction, &QAction::triggered, this, &MessengerPage::coinControlClipboardAmount);
    connect(clipboardFeeAction, &QAction::triggered, this, &MessengerPage::coinControlClipboardFee);
    connect(clipboardAfterFeeAction, &QAction::triggered, this, &MessengerPage::coinControlClipboardAfterFee);
    connect(clipboardBytesAction, &QAction::triggered, this, &MessengerPage::coinControlClipboardBytes);
    connect(clipboardLowOutputAction, &QAction::triggered, this, &MessengerPage::coinControlClipboardLowOutput);
    connect(clipboardChangeAction, &QAction::triggered, this, &MessengerPage::coinControlClipboardChange);
    ui->labelCoinControlQuantity->addAction(clipboardQuantityAction);
    ui->labelCoinControlAmount->addAction(clipboardAmountAction);
    ui->labelCoinControlFee->addAction(clipboardFeeAction);
    ui->labelCoinControlAfterFee->addAction(clipboardAfterFeeAction);
    ui->labelCoinControlBytes->addAction(clipboardBytesAction);
    ui->labelCoinControlLowOutput->addAction(clipboardLowOutputAction);
    ui->labelCoinControlChange->addAction(clipboardChangeAction);


    // init transaction fee section
    QSettings settings;
    if (!settings.contains("fFeeSectionMinimized"))
        settings.setValue("fFeeSectionMinimized", true);
    if (!settings.contains("nFeeRadio") && settings.contains("nTransactionFee") && settings.value("nTransactionFee").toLongLong() > 0) // compatibility
        settings.setValue("nFeeRadio", 1); // custom
    if (!settings.contains("nFeeRadio"))
        settings.setValue("nFeeRadio", 0); // recommended
    if (!settings.contains("nSmartFeeSliderPosition"))
        settings.setValue("nSmartFeeSliderPosition", 0);
    if (!settings.contains("nTransactionFee"))
        settings.setValue("nTransactionFee", (qint64)DEFAULT_PAY_TX_FEE);
    if (!settings.contains("fPayOnlyMinFee"))
        settings.setValue("fPayOnlyMinFee", false);
    groupFee = new QButtonGroup(this);
    groupFee->addButton(ui->radioSmartFee);
    groupFee->addButton(ui->radioCustomFee);
    groupFee->setId(ui->radioSmartFee, 0);
    groupFee->setId(ui->radioCustomFee, 1);
    groupFee->button((int)std::max(0, std::min(1, settings.value("nFeeRadio").toInt())))->setChecked(true);
    ui->customFee->setValue(settings.value("nTransactionFee").toLongLong());
    ui->checkBoxMinimumFee->setChecked(settings.value("fPayOnlyMinFee").toBool());
    minimizeFeeSection(settings.value("fFeeSectionMinimized").toBool());

    ui->transactionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->messageViewEdit->setReadOnly(true);

    connect(ui->sendButton, SIGNAL(clicked()), this, SLOT(send()));
    connect(ui->tabWidget, SIGNAL(currentChanged(int)), this, SLOT(on_tabChanged()));
    connect(ui->transactionTable, SIGNAL(cellClicked(int, int)), this, SLOT(on_transactionsTableCellSelected(int, int)));
}

MessengerPage::~MessengerPage()
{
    delete ui;
}

void MessengerPage::minimizeFeeSection(bool fMinimize)
{
    ui->labelFeeMinimized->setVisible(fMinimize);
    ui->buttonChooseFee  ->setVisible(fMinimize);
    ui->buttonMinimizeFee->setVisible(!fMinimize);
    ui->frameFeeSelection->setVisible(!fMinimize);
    ui->horizontalLayoutSmartFee->setContentsMargins(0, (fMinimize ? 0 : 6), 0, 0);
    fFeeMinimized = fMinimize;
}

void MessengerPage::on_buttonChooseFee_clicked()
{
    minimizeFeeSection(false);
}

void MessengerPage::on_buttonMinimizeFee_clicked()
{
    updateFeeMinimizedLabel();
    minimizeFeeSection(true);
}

void MessengerPage::updateFeeMinimizedLabel()
{
    if(!walletModel || !walletModel->getOptionsModel())
        return;

    if (ui->radioSmartFee->isChecked())
        ui->labelFeeMinimized->setText(ui->labelSmartFee->text());
    else {
        ui->labelFeeMinimized->setText(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), ui->customFee->value()) + "/kB");
    }
}

void MessengerPage::updateMinFeeLabel()
{
    if (walletModel && walletModel->getOptionsModel())
        ui->checkBoxMinimumFee->setText(tr("Pay only the required fee of %1").arg(
            BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), walletModel->wallet().getRequiredFee(1000)) + "/kB")
        );
}

void MessengerPage::updateCoinControlState(CCoinControl& ctrl)
{
    if (ui->radioCustomFee->isChecked()) {
        ctrl.m_feerate = CFeeRate(ui->customFee->value());
    } else {
        ctrl.m_feerate.reset();
    }
    // Avoid using global defaults when sending money from the GUI
    // Either custom fee will be used or if not selected, the confirmation target from dropdown box
    ctrl.m_confirm_target = getConfTargetForIndex(ui->confTargetSelector->currentIndex());
    ctrl.m_signal_bip125_rbf = ui->optInRBF->isChecked();
}

void MessengerPage::updateSmartFeeLabel()
{
    if(!walletModel || !walletModel->getOptionsModel())
        return;
    CCoinControl coin_control;
    updateCoinControlState(coin_control);
    coin_control.m_feerate.reset(); // Explicitly use only fee estimation rate for smart fee labels
    int returned_target;
    FeeReason reason;
    feeRate = CFeeRate(walletModel->wallet().getMinimumFee(1000, coin_control, &returned_target, &reason));

    ui->labelSmartFee->setText(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), feeRate.GetFeePerK()) + "/kB");

    if (reason == FeeReason::FALLBACK) {
        ui->labelSmartFee2->show(); // (Smart fee not initialized yet. This usually takes a few blocks...)
        ui->labelFeeEstimation->setText("");
        ui->fallbackFeeWarningLabel->setVisible(true);
        int lightness = ui->fallbackFeeWarningLabel->palette().color(QPalette::WindowText).lightness();
        QColor warning_colour(255 - (lightness / 5), 176 - (lightness / 3), 48 - (lightness / 14));
        ui->fallbackFeeWarningLabel->setStyleSheet("QLabel { color: " + warning_colour.name() + "; }");
        ui->fallbackFeeWarningLabel->setIndent(QFontMetrics(ui->fallbackFeeWarningLabel->font()).width("x"));
    }
    else
    {
        ui->labelSmartFee2->hide();
        ui->labelFeeEstimation->setText(tr("Estimated to begin confirmation within %n block(s).", "", returned_target));
        ui->fallbackFeeWarningLabel->setVisible(false);
    }

    updateFeeMinimizedLabel();
}

void MessengerPage::setMinimumFee()
{
    ui->customFee->setValue(walletModel->wallet().getRequiredFee(1000));
}

void MessengerPage::updateFeeSectionControls()
{
    ui->confTargetSelector      ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee           ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee2          ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee3          ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelFeeEstimation      ->setEnabled(ui->radioSmartFee->isChecked());
    ui->checkBoxMinimumFee      ->setEnabled(ui->radioCustomFee->isChecked());
    ui->labelMinFeeWarning      ->setEnabled(ui->radioCustomFee->isChecked());
    ui->labelCustomPerKilobyte  ->setEnabled(ui->radioCustomFee->isChecked() && !ui->checkBoxMinimumFee->isChecked());
    ui->customFee               ->setEnabled(ui->radioCustomFee->isChecked() && !ui->checkBoxMinimumFee->isChecked());
}

void MessengerPage::setBalance(const interfaces::WalletBalances& balances)
{
    if(walletModel && walletModel->getOptionsModel())
    {
        ui->labelBalance->setText(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), balances.balance));
    }
}

void MessengerPage::updateDisplayUnit()
{
    setBalance(walletModel->wallet().getBalances());
    ui->customFee->setDisplayUnit(walletModel->getOptionsModel()->getDisplayUnit());
    updateMinFeeLabel();
    updateSmartFeeLabel();
}

void MessengerPage::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;

    if (_clientModel) {
        connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(updateSmartFeeLabel()));
    }
}

void MessengerPage::setModel(WalletModel *model)
{
    walletModel = model;

    interfaces::WalletBalances balances = walletModel->wallet().getBalances();
    setBalance(balances);
    connect(walletModel, SIGNAL(balanceChanged(interfaces::WalletBalances)), this, SLOT(setBalance(interfaces::WalletBalances)));
    connect(walletModel->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

    for (const int n : confTargets) {
        ui->confTargetSelector->addItem(tr("%1 (%2 blocks)").arg(GUIUtil::formatNiceTimeOffset(n*Params().GetConsensus().nPowTargetSpacing)).arg(n));
    }
    connect(ui->confTargetSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(updateSmartFeeLabel()));
    connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(setMinimumFee()));
    connect(groupFee, SIGNAL(buttonClicked(int)), this, SLOT(updateFeeSectionControls()));
    connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(updateFeeSectionControls()));
    connect(ui->optInRBF, SIGNAL(stateChanged(int)), this, SLOT(updateSmartFeeLabel()));
    ui->customFee->setSingleStep(model->wallet().getRequiredFee(1000));
    updateFeeSectionControls();
    updateMinFeeLabel();
    updateSmartFeeLabel();

    // set default rbf checkbox state
    ui->optInRBF->setCheckState(Qt::Checked);

    // set the smartfee-sliders default value (wallets default conf.target or last stored value)
    QSettings settings;
    if (settings.value("nSmartFeeSliderPosition").toInt() != 0) {
        // migrate nSmartFeeSliderPosition to nConfTarget
        // nConfTarget is available since 0.15 (replaced nSmartFeeSliderPosition)
        int nConfirmTarget = 25 - settings.value("nSmartFeeSliderPosition").toInt(); // 25 == old slider range
        settings.setValue("nConfTarget", nConfirmTarget);
        settings.remove("nSmartFeeSliderPosition");
    }
    if (settings.value("nConfTarget").toInt() == 0)
        ui->confTargetSelector->setCurrentIndex(getIndexForConfTarget(model->wallet().getConfirmTarget()));
    else
        ui->confTargetSelector->setCurrentIndex(getIndexForConfTarget(settings.value("nConfTarget").toInt()));

    // Coin Control
    connect(walletModel->getOptionsModel(), &OptionsModel::displayUnitChanged, this, &MessengerPage::coinControlUpdateLabels);
    connect(walletModel->getOptionsModel(), &OptionsModel::coinControlFeaturesChanged, this, &MessengerPage::coinControlFeatureChanged);
    ui->frameCoinControl->setVisible(walletModel->getOptionsModel()->getCoinControlFeatures());
    coinControlUpdateLabels();
}

void MessengerPage::showEvent(QShowEvent * event)
{
    coinControlUpdateLabels();
}


// Coin Control: copy label "Quantity" to clipboard
void MessengerPage::coinControlClipboardQuantity()
{
    GUIUtil::setClipboard(ui->labelCoinControlQuantity->text());
}

// Coin Control: copy label "Amount" to clipboard
void MessengerPage::coinControlClipboardAmount()
{
    GUIUtil::setClipboard(ui->labelCoinControlAmount->text().left(ui->labelCoinControlAmount->text().indexOf(" ")));
}

// Coin Control: copy label "Fee" to clipboard
void MessengerPage::coinControlClipboardFee()
{
    GUIUtil::setClipboard(ui->labelCoinControlFee->text().left(ui->labelCoinControlFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "After fee" to clipboard
void MessengerPage::coinControlClipboardAfterFee()
{
    GUIUtil::setClipboard(ui->labelCoinControlAfterFee->text().left(ui->labelCoinControlAfterFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "Bytes" to clipboard
void MessengerPage::coinControlClipboardBytes()
{
    GUIUtil::setClipboard(ui->labelCoinControlBytes->text().replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "Dust" to clipboard
void MessengerPage::coinControlClipboardLowOutput()
{
    GUIUtil::setClipboard(ui->labelCoinControlLowOutput->text());
}

// Coin Control: copy label "Change" to clipboard
void MessengerPage::coinControlClipboardChange()
{
    GUIUtil::setClipboard(ui->labelCoinControlChange->text().left(ui->labelCoinControlChange->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: settings menu - coin control enabled/disabled by user
void MessengerPage::coinControlFeatureChanged(bool checked)
{
    ui->frameCoinControl->setVisible(checked);

    if (!checked && walletModel) // coin control features disabled
        CoinControlDialog::coinControl()->SetNull();

    coinControlUpdateLabels();
}

// Coin Control: button inputs -> show actual coin control dialog
void MessengerPage::coinControlButtonClicked()
{
    CoinControlDialog dlg(platformStyle);
    dlg.setModel(walletModel);
    dlg.exec();
    coinControlUpdateLabels();
}

// Coin Control: checkbox custom change address
void MessengerPage::coinControlChangeChecked(int state)
{
    if (state == Qt::Unchecked)
    {
        CoinControlDialog::coinControl()->destChange = CNoDestination();
        ui->labelCoinControlChangeLabel->clear();
    }
    else
        // use this to re-validate an already entered address
        coinControlChangeEdited(ui->lineEditCoinControlChange->text());

    ui->lineEditCoinControlChange->setEnabled((state == Qt::Checked));
}

// Coin Control: custom change address changed
void MessengerPage::coinControlChangeEdited(const QString& text)
{
    if (walletModel && walletModel->getAddressTableModel())
    {
        // Default to no change address until verified
        CoinControlDialog::coinControl()->destChange = CNoDestination();
        ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:red;}");

        const CTxDestination dest = DecodeDestination(text.toStdString());

        if (text.isEmpty()) // Nothing entered
        {
            ui->labelCoinControlChangeLabel->setText("");
        }
        else if (!IsValidDestination(dest)) // Invalid address
        {
            ui->labelCoinControlChangeLabel->setText(tr("Warning: Invalid BST address"));
        }
        else // Valid address
        {
            if (!walletModel->wallet().isSpendable(dest)) {
                ui->labelCoinControlChangeLabel->setText(tr("Warning: Unknown change address"));

                // confirmation dialog
                QMessageBox::StandardButton btnRetVal = QMessageBox::question(this, tr("Confirm custom change address"), tr("The address you selected for change is not part of this wallet. Any or all funds in your wallet may be sent to this address. Are you sure?"),
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

                if(btnRetVal == QMessageBox::Yes)
                    CoinControlDialog::coinControl()->destChange = dest;
                else
                {
                    ui->lineEditCoinControlChange->setText("");
                    ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:black;}");
                    ui->labelCoinControlChangeLabel->setText("");
                }
            }
            else // Known change address
            {
                ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:black;}");

                // Query label
                QString associatedLabel = walletModel->getAddressTableModel()->labelForAddress(text);
                if (!associatedLabel.isEmpty())
                    ui->labelCoinControlChangeLabel->setText(associatedLabel);
                else
                    ui->labelCoinControlChangeLabel->setText(tr("(no label)"));

                CoinControlDialog::coinControl()->destChange = dest;
            }
        }
    }
}

// Coin Control: update labels
void MessengerPage::coinControlUpdateLabels()
{
    if (!walletModel || !walletModel->getOptionsModel())
        return;

    updateCoinControlState(*CoinControlDialog::coinControl());

    // set pay amounts
    CoinControlDialog::payAmounts.clear();
    CoinControlDialog::fSubtractFeeFromAmount = false;

    CAmount camount = 0;
    CoinControlDialog::payAmounts.append(camount);
    //CoinControlDialog::fSubtractFeeFromAmount = true;

    if (CoinControlDialog::coinControl()->HasSelected())
    {
        // actual coin control calculation
        CoinControlDialog::updateLabels(walletModel, ui->widgetCoinControl, false, 0);

        // show coin control stats
        ui->labelCoinControlAutomaticallySelected->hide();
        ui->widgetCoinControl->show();
    }
    else
    {
        // hide coin control stats
        ui->labelCoinControlAutomaticallySelected->show();
        ui->widgetCoinControl->hide();
        ui->labelCoinControlInsuffFunds->hide();
    }
}

void MessengerPage::unlockWallet()
{
    if (walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        AskPassphraseDialog dlg(AskPassphraseDialog::Unlock, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
}

void MessengerPage::on_tabChanged()
{
    ////TODO: just for test
    std::shared_ptr<CWallet> wallet = GetWallets()[0];
    CWallet* pwallet=nullptr;
    if(wallet!=nullptr)
    {
        pwallet=wallet.get();
        this->fillUpTable(pwallet->encrMsgMapWallet);
    }
}

void MessengerPage::on_transactionsTableCellSelected(int row, int col)
{
    QTableWidgetItem* item = ui->transactionTable->item(row, 1);
    QString txnId = item->text();
    read(txnId.toUtf8().constData());
}

void MessengerPage::read(const std::string& txnId)
{
#ifdef ENABLE_WALLET
    if (walletModel)
    {
        try
        {
            interfaces::Wallet& wlt = walletModel->wallet();
            std::shared_ptr<CWallet> wallet = GetWallet(wlt.getWalletName());
            CWallet* pwallet = nullptr;
            if (wallet != nullptr)
            {
                pwallet = wallet.get();
            }

            std::string privateRsaKey;
            WalletDatabase& dbh = pwallet->GetMsgDBHandle();
            WalletBatch batch(dbh);
            batch.ReadPrivateKey(privateRsaKey);

            RetrieveDataTxs retrieveDataTxs(txnId, pwallet);
            std::vector<char> OPreturnData = retrieveDataTxs.getTxData();

            std::vector<unsigned char> decryptedData = createDecryptedMessage(
                        reinterpret_cast<unsigned char*>(OPreturnData.data()),
                        OPreturnData.size(),
                        privateRsaKey.c_str());

            ui->messageViewEdit->setPlainText(std::string(decryptedData.begin(), decryptedData.end()).c_str());
        }
        catch(std::exception const& e)
        {
            QMessageBox msgBox;
            msgBox.setText(e.what());
            msgBox.exec();
        }
        catch(...)
        {
            QMessageBox msgBox;
            msgBox.setText("Unknown exception occured");
            msgBox.exec();
        }
    }
#endif
}

void MessengerPage::send()
{
#ifdef ENABLE_WALLET
    if(walletModel)
    {
        try
        {
            interfaces::Wallet& wlt = walletModel->wallet();
            std::shared_ptr<CWallet> wallet = GetWallet(wlt.getWalletName());
            if(wallet != nullptr)
            {
                CWallet* const pwallet=wallet.get();

                pwallet->BlockUntilSyncedToCurrentChain();

                LOCK2(cs_main, pwallet->cs_wallet);

                CAmount curBalance = pwallet->GetBalance();

                std::vector<unsigned char> data = getData();

                CRecipient recipient;
                recipient.scriptPubKey << OP_RETURN << data;
                recipient.nAmount=0;
                recipient.fSubtractFeeFromAmount=false;

                std::vector<CRecipient> vecSend;
                vecSend.push_back(recipient);

                CReserveKey reservekey(pwallet);
                CAmount nFeeRequired;
                int nChangePosInOut=1;
                std::string strFailReason;
                CTransactionRef tx;

                unlockWallet();

                // Always use a CCoinControl instance, use the CoinControlDialog instance if CoinControl has been enabled
                CCoinControl coin_control;
                if (walletModel->getOptionsModel()->getCoinControlFeatures())
                {
                    coin_control = *CoinControlDialog::coinControl();
                }
                updateCoinControlState(coin_control);
                coinControlUpdateLabels();

                if(!pwallet->CreateTransaction(vecSend, nullptr, tx, reservekey, nFeeRequired, nChangePosInOut, strFailReason, coin_control))
                {
                    if (nFeeRequired > curBalance)
                    {
                        strFailReason = strprintf("Error: This transaction requires a transaction fee of at least %s", FormatMoney(nFeeRequired));
                    }
                    throw std::runtime_error(std::string("CreateTransaction failed with reason: ")+strFailReason);
                }

                CValidationState state;
                if(!pwallet->CommitTransaction(tx, {}, {}, reservekey, g_connman.get(), state))
                {
                    throw std::runtime_error(std::string("CommitTransaction failed with reason: ")+FormatStateMessage(state));
                }

                QString qtxid=QString::fromStdString(tx->GetHash().GetHex());

                StoreTxDialog *dlg = new StoreTxDialog(qtxid, static_cast<double>(nFeeRequired)/COIN, walletModel->getOptionsModel()->getDisplayUnit());
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->show();

            }
            else
            {
                throw std::runtime_error(std::string("No wallet found"));
            }
        }
        catch(std::exception const& e)
        {
            QMessageBox msgBox;
            msgBox.setText(e.what());
            msgBox.exec();
        }
        catch(...)
        {
            QMessageBox msgBox;
            msgBox.setText("Unknown exception occured");
            msgBox.exec();
        }
    }
#endif
}

std::vector<unsigned char> MessengerPage::getData()
{
    std::string msg = ui->messageStoreEdit->toPlainText().toUtf8().constData();
    if (msg.length()>maxDataSize)
    {
        throw std::runtime_error(strprintf("Data size is grater than %d bytes", maxDataSize));
    }
    std::string publicKey = ui->addressEdit->toPlainText().toUtf8().constData();
    if (publicKey.empty())
    {
        throw std::runtime_error("Missing receiver public key, message can't be encrypted");
    }

    std::vector<unsigned char> retData = createEncryptedMessage(
        reinterpret_cast<const unsigned char*>(msg.c_str()),
        msg.length(),
        publicKey.c_str());

    return retData;
}

void MessengerPage::fillUpTable(std::map<uint256, CWalletTx> &transactions)
{
    ui->transactionTable->setRowCount(transactions.size());

    int row = 0;
    for (auto &it : transactions)
    {
        //TODO: use QDateTime instead
        time_t t = it.second.nTimeReceived;
        std::tm *ptm = std::localtime(&t);
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%d.%m.%Y %H:%M", ptm);

        ui->transactionTable->setItem(row, 0, new QTableWidgetItem(buffer));
        ui->transactionTable->setItem(row, 1, new QTableWidgetItem(it.first.ToString().c_str()));
        ++row;
    }

}