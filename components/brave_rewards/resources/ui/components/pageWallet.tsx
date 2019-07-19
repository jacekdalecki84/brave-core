/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import * as React from 'react'
import { bindActionCreators, Dispatch } from 'redux'
import { connect } from 'react-redux'
// Components
import {
  ModalActivity,
  ModalBackupRestore,
  ModalPending,
  PrintableActivity,
  WalletSummary,
  WalletWrapper
} from 'brave-ui/features/rewards'
import { WalletAddIcon } from 'brave-ui/components/icons'
import { AlertWallet } from 'brave-ui/features/rewards/walletWrapper'
import { Provider } from 'brave-ui/features/rewards/profile'
import { DetailRow as PendingDetailRow, PendingType } from 'brave-ui/features/rewards/tablePending'
import { DetailRow as ContributeRows } from 'brave-ui/features/rewards/tableContribute'
import { DetailRow as TransactionRow, TransactionType } from 'brave-ui/features/rewards/tableTransactions'
// Utils
import { getLocale } from '../../../../common/locale'
import * as rewardsActions from '../actions/rewards_actions'
import * as utils from '../utils'
import WalletOff from 'brave-ui/features/rewards/walletOff'
import ModalAddFunds from 'brave-ui/features/rewards/modalAddFunds'

const clipboardCopy = require('clipboard-copy')

interface State {
  activeTabId: number
  modalBackup: boolean
  modalActivity: boolean
  modalAddFunds: boolean
  modalPendingContribution: boolean
}

interface Props extends Rewards.ComponentProps {
  isPrint: boolean
}

class PageWallet extends React.Component<Props, State> {
  constructor (props: Props) {
    super(props)
    this.state = {
      activeTabId: 0,
      modalBackup: false,
      modalActivity: false,
      modalAddFunds: false,
      modalPendingContribution: false
    }
  }

  get actions () {
    return this.props.actions
  }

  componentDidMount () {
    this.isAddFundsUrl()
    this.isBackupUrl()
    if (this.props.isPrint) {
      window.print()
    }
    const currentTime = new Date()
    const month = currentTime.getMonth() + 1
    const year = currentTime.getFullYear()
    this.actions.clearStatements()
    this.getMonthlyStatementItems(month, year)
  }

  clearStatements = (month: number, year: number) => {
    this.actions.clearStatements()
    this.getMonthlyStatementItems(month, year)
  }

  getMonthlyStatementItems (month: number, year: number) {
    this.getOneTimeTipStatements(month, year, (publishers: Rewards.Publisher[]) => {
      this.actions.addOneTimeTipContribution(publishers)
    })

    this.getRecurringTipStatements(month, year, (contributions: Rewards.Contribution[]) => {
      for (const contribution of contributions) {
        this.getPublisherInfo(contribution.publisherKey, (publisher: Rewards.Publisher) => {
          this.actions.addRecurringTipContribution(contribution, publisher)
        })
      }
    })

    this.getAutoContributeStatements(month, year, (contributions: Rewards.Contribution[]) => {
      for (const contribution of contributions) {
        this.getPublisherInfo(contribution.publisherKey, (publisher: Rewards.Publisher) => {
          this.actions.addAutoContribution(contribution, publisher)
        })
      }
    })

    this.getTransactions(month, year, (contributions: Rewards.Contribution[]) => {
      this.actions.addTransactions(contributions)
    })
  }

  getOneTimeTipStatements = (month: number, year: number, callback: (publisher: Rewards.Publisher[]) => void) => {
    window.cr.sendWithPromise('brave_rewards.getOneTimeTipsStatements',
      month,
      year).then((publishers: Rewards.Publisher[]) => {
      callback(publishers)
    })
  }

  getRecurringTipStatements = (month: number, year: number, callback: (contributions: Rewards.Contribution[]) => void) => {
    window.cr.sendWithPromise('brave_rewards.getRecurringTipsStatements',
    month,
    year).then((contributions: Rewards.Contribution[]) => {
      callback(contributions)
    })
  }

  getAutoContributeStatements = (month: number, year: number, callback: (contributions: Rewards.Contribution[]) => void) => {
    window.cr.sendWithPromise('brave_rewards.getAutoContributeStatements',
    month,
    year).then((contributions: Rewards.Contribution[]) => {
      callback(contributions)
    })
  }

  getTransactions = (month: number, year: number, callback: (contributions: Rewards.Contribution[]) => void) => {
    window.cr.sendWithPromise('brave_rewards.getTransactionStatements',
    month,
    year).then((contributions: Rewards.Contribution[]) => {
      callback(contributions)
    })
  }

  getPublisherInfo = (publisherKey: string, callback: (publisher: Rewards.Publisher) => void) => {
    window.cr.sendWithPromise('brave_rewards.getPublisherInfo',
      publisherKey
    ).then((publisher: Rewards.Publisher) => {
      callback(publisher)
    })
  }


  onModalBackupClose = () => {
    if (this.urlHashIs('#backup-restore')) {
      window.location.hash = ''
    }
    this.actions.onModalBackupClose()
  }

  onModalBackupOpen = () => {
    if (this.props.rewardsData.recoveryKey.length === 0) {
      this.actions.getWalletPassphrase()
    }

    this.actions.onModalBackupOpen()
  }

  onModalBackupTabChange = () => {
    const newTabId = this.state.activeTabId === 0 ? 1 : 0
    this.setState({
      activeTabId: newTabId
    })
  }

  onModalBackupOnCopy = async (backupKey: string) => {
    // TODO(jsadler) possibly flash a message that copy was completed
    try {
      await clipboardCopy(backupKey)
      console.log('Copy successful')
      chrome.send('brave_rewards.setBackupCompleted')
    } catch (e) {
      console.log('Copy failed')
    }
  }

  onModalBackupOnPrint = (backupKey: string) => {
    if (document.location) {
      const win = window.open(document.location.href)
      if (win) {
        win.document.body.innerText = utils.constructBackupString(backupKey) // this should be text, not HTML
        win.print()
        win.close()
        chrome.send('brave_rewards.setBackupCompleted')
      }
    }
  }

  onModalBackupOnSaveFile = (backupKey: string) => {
    const backupString = utils.constructBackupString(backupKey)
    const backupFileText = 'brave_wallet_recovery.txt'
    const a = document.createElement('a')
    document.body.appendChild(a)
    a.style.display = 'display: none'
    const blob = new Blob([backupString], { type : 'plain/text' })
    const url = window.URL.createObjectURL(blob)
    a.href = url
    a.download = backupFileText
    a.click()
    window.URL.revokeObjectURL(url)
    chrome.send('brave_rewards.setBackupCompleted')
  }

  onModalBackupOnRestore = (key: string | MouseEvent) => {
    if (typeof key === 'string' && key.length > 0) {
      key = this.pullRecoveryKeyFromFile(key)
      this.actions.recoverWallet(key)
    }
  }

  pullRecoveryKeyFromFile = (key: string) => {
    let recoveryKey = null
    if (key) {
      let messageLines = key.match(/^.+$/gm)
      if (messageLines) {
        let passphraseLine = '' || messageLines[2]
        if (passphraseLine) {
          const passphrasePattern = new RegExp(['Recovery Key:', '(.+)$'].join(' '))
          recoveryKey = (passphraseLine.match(passphrasePattern) || [])[1]
          return recoveryKey
        }
      }
    }
    return key
  }

  onModalBackupOnImport = () => {
    // TODO NZ implement
    console.log('onModalBackupOnImport')
  }

  getConversion = () => {
    const balance = this.props.rewardsData.balance
    return utils.convertBalance(balance.total.toString(), balance.rates)
  }

  getGrants = () => {
    const grants = this.props.rewardsData.walletInfo.grants
    if (!grants) {
      return []
    }

    return grants.map((grant: Rewards.Grant) => {
      return {
        tokens: utils.convertProbiToFixed(grant.probi),
        expireDate: new Date(grant.expiryTime * 1000).toLocaleDateString(),
        type: grant.type || 'ugp'
      }
    })
  }

  onModalActivityToggle = () => {
    this.setState({
      modalActivity: !this.state.modalActivity
    })
  }

  onModalAddFundsToggle = () => {
    this.setState({
      modalAddFunds: !this.state.modalAddFunds
    })
  }

  urlHashIs = (hash: string) => {
    return (
      window &&
      window.location &&
      window.location.hash &&
      window.location.hash === hash
    )
  }

  onModalPendingToggle = () => {
    this.setState({
      modalPendingContribution: !this.state.modalPendingContribution
    })
  }

  isAddFundsUrl = () => {
    if (this.urlHashIs('#add-funds')) {
      this.setState({
        modalAddFunds: true
      })
    } else {
      this.setState({
        modalAddFunds: false
      })
    }
  }

  isBackupUrl = () => {
    if (this.urlHashIs('#backup-restore')) {
      this.onModalBackupOpen()
    }
  }

  closeModalAddFunds = () => {
    if (this.urlHashIs('#add-funds')) {
      window.location.hash = ''
    }
    this.onModalAddFundsToggle()
  }

  onModalActivityAction (action: string) {
    // TODO NZ implement
    console.log(action)
  }

  onModalActivityRemove () {
    // TODO NZ implement
    console.log('onModalActivityRemove')
  }

  walletAlerts = (): AlertWallet | null => {
    const { total } = this.props.rewardsData.balance
    const {
      walletRecoverySuccess,
      walletServerProblem,
      walletCorrupted
    } = this.props.rewardsData.ui

    if (walletServerProblem) {
      return {
        node: <><b>{getLocale('uhOh')}</b> {getLocale('serverNotResponding')}</>,
        type: 'error'
      }
    }

    if (walletRecoverySuccess) {
      return {
        node: <><b>{getLocale('walletRestored')}</b> {getLocale('walletRecoverySuccess', { balance: total.toString() })}</>,
        type: 'success',
        onAlertClose: () => {
          this.actions.onClearAlert('walletRecoverySuccess')
        }
      }
    }

    if (walletCorrupted) {
      return {
        node: (
          <>
            <b>{getLocale('uhOh')}</b> {getLocale('walletCorrupted')} <a href={'#'} style={{ 'color': '#838391' }} onClick={this.onModalBackupOpen}>
               {getLocale('walletCorruptedNow')}
             </a>
          </>
        ),
        type: 'error'
      }
    }

    return null
  }

  getWalletSummary = () => {
    const { balance, reports, pendingContributionTotal } = this.props.rewardsData
    const { rates } = balance

    let props = {}

    const currentTime = new Date()
    const reportKey = `${currentTime.getFullYear()}_${currentTime.getMonth() + 1}`
    const report: Rewards.Report = reports[reportKey]
    if (report) {
      for (let key in report) {
        const item = report[key]

        if (item.length > 1 && key !== 'total') {
          const tokens = utils.convertProbiToFixed(item)
          props[key] = {
            tokens,
            converted: utils.convertBalance(tokens, rates)
          }
        }
      }
    }

    let result: {report: Record<string, {tokens: string, converted: string}>, onSeeAllReserved?: () => {}} = {
      report: props,
      onSeeAllReserved: undefined
    }

    if (pendingContributionTotal > 0) {
      result.onSeeAllReserved = this.onModalPendingToggle.bind(this)
    }

    return result
  }

  getPendingRows = (): PendingDetailRow[] => {
    const { balance, pendingContributions } = this.props.rewardsData
    return pendingContributions.map((item: Rewards.PendingContribution) => {
      let faviconUrl = `chrome://favicon/size/48@1x/${item.url}`
      if (item.favIcon && item.verified) {
        faviconUrl = `chrome://favicon/size/48@1x/${item.favIcon}`
      }

      let type: PendingType = 'ac'

      if (item.category === 8) { // one time tip
        type = 'tip'
      } else if (item.category === 16) { // recurring tip
        type = 'recurring'
      }

      return {
        profile: {
          name: item.name,
          verified: item.verified,
          provider: (item.provider ? item.provider : undefined) as Provider,
          src: faviconUrl
        },
        url: item.url,
        type,
        amount: {
          tokens: item.amount.toFixed(1),
          converted: utils.convertBalance(item.amount.toString(), balance.rates)
        },
        date: new Date(parseInt(item.expirationDate, 10) * 1000).toLocaleDateString(),
        onRemove: () => {
          this.actions.removePendingContribution(item.publisherKey, item.viewingId, item.addedDate)
        }
      }
    })
  }

  removeAllPendingContribution = () => {
    this.actions.removeAllPendingContribution()
  }

  getMonthlyStatementTransaction = (): TransactionRow[] => {
    const { monthlyStatement, balance } = this.props.rewardsData
    const transactions = monthlyStatement.transactions
    return transactions.map((item: Rewards.MonthlyTransaction) => {
      const amount = utils.convertProbiToFixed(item.probi)
      switch (item.category) {
        case 2:  // auto-contribute
          const acDate = `${utils.getShortMonthName(item.date)} ${utils.getDayOfMonthFromTimestampSeconds(item.date)}`
          return {
            date:  acDate || '',
            description: getLocale('autoContribute'),
            type: 'contributeTx' as TransactionType,
            amount: {
              value: amount,
              converted: utils.convertBalance(amount, balance.rates)
            }
          }
        case 3:  // grants
          const grantDate = utils.getLongMonthName(item.date)
          return {
            date: grantDate || '',
            description: getLocale('grantsStatement'),
            type: 'grantsTx' as TransactionType,
            amount: {
              value: amount,
              converted: utils.convertBalance(amount, balance.rates)
            }
          }
        case 5:  // ads earnings
          const adsDate = utils.getLongMonthName(item.date)
          return {
            date: adsDate || '',
            description: getLocale('adsStatement'),
            type: 'earningFromAdsTx' as TransactionType,
            amount: {
              value: amount,
              converted: utils.convertBalance(amount, balance.rates)
            }
          }
        case 8:  // tips
          const tipsDate = `${utils.getShortMonthName(item.date)} ${utils.getDayOfMonthFromTimestampSeconds(item.date)}`
          return {
            date: tipsDate || '',
            description: getLocale('oneTimeTip'),
            type: 'donationTx' as TransactionType,
            amount: {
              value: amount,
              converted: utils.convertBalance(amount, balance.rates)
            }
          }
        case 16:  // monthly
          const monthlyDate = `${utils.getShortMonthName(item.date)} ${utils.getDayOfMonthFromTimestampSeconds(item.date)}`
          return {
            date: monthlyDate || '',
            description: getLocale('monthlyDonation'),
            type: 'recurringDonationTx' as TransactionType,
            amount: {
              value: amount,
              converted: utils.convertBalance(amount, balance.rates)
            }
          }
        default: return {
          date: utils.getShortMonthName(new Date().toString()),
          description: getLocale('grantsStatement'),
          type: 'grantsTx' as TransactionType,
          amount: {
            value: '0.0',
            converted: '0.00'
          }
        }
      }
    })
  }

  selectPreviousMonthFromCurrent = () => {
    const currentTime = new Date()
    return `${currentTime.getFullYear()}_${currentTime.getMonth() + 1}`
  }

  getMonthlyStatementMonthsAvailable = () => {
    const { monthlyStatement } = this.props.rewardsData
    const monthsAvailable = monthlyStatement.monthsAvailable
    if (!monthsAvailable) {
      const currentTime = new Date()
      const reportKey = `${currentTime.getFullYear()}_${currentTime.getMonth() + 1}`
      return JSON.parse(`{ \"${reportKey}\" : \"${currentTime.toLocaleString(navigator.language, { month: 'long' })} ${currentTime.getFullYear()}\" }`)
    }
    let monthsJson = '{ '
    for (let ix = 0; ix < monthsAvailable.length; ix++) {
      const date = new Date(parseInt(monthsAvailable[ix].split('_')[0], 10), parseInt(monthsAvailable[ix].split('_')[1], 10) - 1)
      const selectionStringValue = date.toLocaleString(navigator.language, { month: 'long' })
      if (ix + 1 === monthsAvailable.length) {
        monthsJson += `\"${monthsAvailable[ix]}\" : \"${selectionStringValue} ${date.getFullYear()}\"`
      } else {
        monthsJson += `\"${monthsAvailable[ix]}\" : \"${selectionStringValue} ${date.getFullYear()}\",`
      }
    }
    monthsJson += ' }'
    return JSON.parse(monthsJson)
  }

  getRows = (rows: Rewards.StatementItem[]): ContributeRows[] => {
    const { balance } = this.props.rewardsData
    return rows.map((item: Rewards.StatementItem) => {
      let faviconUrl = `chrome://favicon/size/48@1x/${item.url}`
      if (item.faviconUrl && item.verified) {
        faviconUrl = `chrome://favicon/size/48@1x/${item.faviconUrl}`
      }
      let tipMonth = ''
      let tipDayOfMonth = ''
      if (item.category === 8) {
        let tipDate = new Date(parseInt(item.date, 10) * 1000)
        tipMonth = tipDate.toLocaleString(navigator.language, { month: 'short' })
        tipDayOfMonth = tipDate.getDate().toString()
      }

      const amountValue = utils.convertProbiToFixed(item.probi)
      return {
        profile: {
          name: item.name,
          verified: item.verified,
          provider: (item.provider ? item.provider : undefined) as Provider,
          src: faviconUrl
        },
        url: item.url,
        attention: parseInt(item.percentage, 10) || 0,
        token: {
          value: amountValue,
          converted: amountValue !== '0.0' ? utils.convertBalance(amountValue, balance.rates) : '0.00'
        },
        tipDate: tipMonth !== '' && tipDayOfMonth !== '' && item.category === 8 ? tipMonth + ' ' + tipDayOfMonth : ''
      }
    })
  }

  getOneTimeTipRows = (rows: Rewards.Publisher[]): ContributeRows[] => {
    const { balance } = this.props.rewardsData
    return rows.map((item: Rewards.Publisher) => {
      let faviconUrl = `chrome://favicon/size/48@1x/${item.url}`
      if (item.favIcon && item.verified) {
        faviconUrl = `chrome://favicon/size/48@1x/${item.favIcon}`
      }
      let tipMonth = ''
      let tipDayOfMonth = ''
      if (item.tipDate) {
        let tipDate = new Date(item.tipDate * 1000)
        tipMonth = tipDate.toLocaleString(navigator.language, { month: 'short' })
        tipDayOfMonth = tipDate.getDate().toString()
      }

      const amountValue = utils.convertProbiToFixed(item.percentage.toString())
      return {
        profile: {
          name: item.name,
          verified: item.verified,
          provider: (item.provider ? item.provider : undefined) as Provider,
          src: faviconUrl
        },
        url: item.url,
        token: {
          value: amountValue,
          converted: amountValue !== '0.0' ? utils.convertBalance(amountValue, balance.rates) : '0.00'
        },
        tipDate: tipMonth !== '' && tipDayOfMonth !== '' ? tipMonth + ' ' + tipDayOfMonth : ''
      }
    })
  }

  getMonthlyRows = (rows: Rewards.MonthlyRecurringTips[] | Rewards.MonthlyAutoContribute[]): ContributeRows[] => {
    const { balance } = this.props.rewardsData
    return rows.map((item: Rewards.MonthlyRecurringTips | Rewards.MonthlyAutoContribute) => {
      let faviconUrl = `chrome://favicon/size/48@1x/${item.publisher.url}`
      if (item.publisher.favIcon && item.publisher.verified) {
        faviconUrl = `chrome://favicon/size/48@1x/${item.publisher.favIcon}`
      }
      let tipMonth = ''
      let tipDayOfMonth = ''
      if (item.contribution.category === 8) {
        let tipDate = new Date(parseInt(item.contribution.date, 10) * 1000)
        tipMonth = tipDate.toLocaleString(navigator.language, { month: 'short' })
        tipDayOfMonth = tipDate.getDate().toString()
      }

      const amountValue = utils.convertProbiToFixed(item.contribution.probi)
      return {
        profile: {
          name: item.publisher.name,
          verified: item.publisher.verified,
          provider: (item.publisher.provider ? item.publisher.provider : undefined) as Provider,
          src: faviconUrl
        },
        url: item.publisher.url,
        token: {
          value: amountValue,
          converted: amountValue !== '0.0' ? utils.convertBalance(amountValue, balance.rates) : '0.00'
        },
        tipDate: tipMonth !== '' && tipDayOfMonth !== '' && item.contribution.category === 8 ? tipMonth + ' ' + tipDayOfMonth : ''
      }
    })
  }

  getMonthlyStatementAutoContribute = (): ContributeRows[] => {
    const { monthlyAutoContribute } = this.props.rewardsData
    return this.getMonthlyRows(monthlyAutoContribute)
  }

  getMonthlyStatementMonthlyContributions = (): ContributeRows[] => {
    const { monthlyRecurringTips } = this.props.rewardsData
    return this.getMonthlyRows(monthlyRecurringTips)
  }

  getMonthlyStatementTips = (): ContributeRows[] => {
    const { monthlyOneTimeTips } = this.props.rewardsData
    return this.getOneTimeTipRows(monthlyOneTimeTips)
  }

  onMonthChange = (key: string) => {
    this.clearStatements((parseInt(key.split('_')[1], 10)), parseInt(key.split('_')[0], 10))
  }

  getMonthlyStatementSummary = () => {
    const { monthlyStatement, balance } = this.props.rewardsData

    const grantValue = monthlyStatement.balanceReport.grant ? utils.convertProbiToFixed(monthlyStatement.balanceReport.grant) : '0.0'
    const adsValue = monthlyStatement.balanceReport.ads ? utils.convertProbiToFixed(monthlyStatement.balanceReport.ads) : '0.0'
    const contributeValue = monthlyStatement.balanceReport ? utils.convertProbiToFixed(monthlyStatement.balanceReport.contribute) : '0.0'
    const donationValue = monthlyStatement.balanceReport ? utils.convertProbiToFixed(monthlyStatement.balanceReport.donation) : '0.0'
    const tipsValue = monthlyStatement.balanceReport ? utils.convertProbiToFixed(monthlyStatement.balanceReport.tips) : '0.0'

    return [
      {
        text: getLocale('tokenGrantsReceived'),
        type: 'grant' as any,
        token: {
          value: grantValue,
          converted: grantValue !== '0.0' ? utils.convertBalance(grantValue, balance.rates) : '0.00'
        }
      },
      {
        text: getLocale('earningsFromAds'),
        type: 'ads' as any,
        token: {
          value: adsValue,
          converted: adsValue !== '0.0' ? utils.convertBalance(adsValue, balance.rates) : '0.00'
        }
      },
      {
        text: getLocale('autoContribute'),
        type: 'contribute' as any,
        token: {
          value: contributeValue,
          converted: contributeValue !== '0.0' ? utils.convertBalance(contributeValue, balance.rates) : '0.00',
          isNegative: contributeValue !== '0.0'
        }
      },
      {
        text: getLocale('monthlyContributions'),
        type: 'recurring' as any,
        token: {
          value: donationValue,
          converted: donationValue !== '0.0' ? utils.convertBalance(donationValue, balance.rates) : '0.00',
          isNegative: donationValue !== '0.0'
        }
      },
      {
        text: getLocale('oneTimeTips'),
        type: 'donations' as any,
        token: {
          value: tipsValue,
          converted: tipsValue !== '0.0' ? utils.convertBalance(tipsValue, balance.rates) : '0.00',
          isNegative: tipsValue !== '0.0'
        }
      }
    ]
  }

  getMonthlyStatementTotal = () => {
    const { monthlyStatement, balance } = this.props.rewardsData
    return {
      value: utils.convertProbiToFixed(monthlyStatement.balanceReport.total),
      converted: utils.convertBalance(utils.convertProbiToFixed(monthlyStatement.balanceReport.total), balance.rates)
    }
  }

  getMonthlyStatementOpenBalance = () => {
    return {
      value: '10.0',
      converted: '5.20'
    }
  }

  getMonthlyStatementCloseBalance = () => {
    return {
      value: '11.0',
      converted: '5.30'
    }
  }

  getPaymentDay = () => {
    return utils.getDayOfMonthFromTimestampSeconds(this.props.rewardsData.reconcileStamp.toString())
  }

  onPrintActivity = () => {
    if (window.location) {
      window.open(window.location.href + '?monthly_print=true')
    }
  }

  onDownloadPDF = () => {
    if (window.location) {
      window.open(window.location.href + '?monthly_pdf=true')
    }
  }

  render () {
    const {
      connectedWallet,
      recoveryKey,
      enabledMain,
      addresses,
      balance,
      ui,
      pendingContributionTotal,
      hasMonthlyStatement
    } = this.props.rewardsData
    console.log(hasMonthlyStatement)
    const { total } = balance
    const { walletRecoverySuccess, modalBackup } = ui
    const addressArray = utils.getAddresses(addresses)

    const pendingTotal = parseFloat((pendingContributionTotal || 0).toFixed(1))

    return (
      <>
        {
          this.props.isPrint ?
            <PrintableActivity
              contributeRows={this.getMonthlyStatementAutoContribute()}
              recurringRows={this.getMonthlyStatementMonthlyContributions()}
              tipRows={this.getMonthlyStatementTips()}
              transactionRows={this.getMonthlyStatementTransaction()}
              months={this.getMonthlyStatementMonthsAvailable()}
              currentMonth={this.selectPreviousMonthFromCurrent()}
              summary={this.getMonthlyStatementSummary()}
              total={this.getMonthlyStatementTotal()}
              paymentDay={this.getPaymentDay()}
              openBalance={{
                value: '10.0',
                converted: '5.20'
              }}
              closingBalance={{
                value: '11.0',
                converted: '5.30'
              }}
            />
            :
            <WalletWrapper
              balance={total.toFixed(1)}
              converted={utils.formatConverted(this.getConversion())}
              actions={[
                {
                  name: getLocale('panelAddFunds'),
                  action: this.onModalAddFundsToggle,
                  icon: <WalletAddIcon />,
                  testId: 'panel-add-funds'
                }
              ]}
              onSettingsClick={this.onModalBackupOpen}
              onActivityClick={this.onModalActivityToggle}
              showCopy={true}
              showSecActions={true}
              grants={this.getGrants()}
              connectedWallet={connectedWallet}
              alert={this.walletAlerts()}
            >
              {
                enabledMain ?
                  <WalletSummary
                    reservedAmount={pendingTotal}
                    reservedMoreLink={'https://brave.com/faq/#unclaimed-funds'}
                    hasActivity={true}
                    onActivity={this.onModalActivityToggle}
                    {...this.getWalletSummary()}
                  />
                : <WalletOff />
              }
            </WalletWrapper>
        }
        {
          modalBackup
            ? <ModalBackupRestore
              activeTabId={this.state.activeTabId}
              backupKey={recoveryKey}
              onTabChange={this.onModalBackupTabChange}
              onClose={this.onModalBackupClose}
              onCopy={this.onModalBackupOnCopy}
              onPrint={this.onModalBackupOnPrint}
              onSaveFile={this.onModalBackupOnSaveFile}
              onRestore={this.onModalBackupOnRestore}
              error={walletRecoverySuccess === false ? getLocale('walletRecoveryFail') : ''}
            />
            : null
        }
        {
          this.state.modalAddFunds
            ? <ModalAddFunds
              onClose={this.closeModalAddFunds}
              addresses={addressArray}
            />
            : null
        }
        {
          this.state.modalPendingContribution
            ? <ModalPending
              onClose={this.onModalPendingToggle}
              rows={this.getPendingRows()}
              onRemoveAll={this.removeAllPendingContribution}
            />
            : null
        }
        {
          this.state.modalActivity ?
            <ModalActivity
              contributeRows={this.getMonthlyStatementAutoContribute()}
              recurringRows={this.getMonthlyStatementMonthlyContributions()}
              tipRows={this.getMonthlyStatementTips()}
              transactionRows={this.getMonthlyStatementTransaction()}
              onClose={this.onModalActivityToggle}
              onPrint={this.onPrintActivity}
              onDownloadPDF={this.onDownloadPDF}
              onMonthChange={this.onMonthChange}
              months={this.getMonthlyStatementMonthsAvailable()}
              currentMonth={this.selectPreviousMonthFromCurrent()}
              summary={this.getMonthlyStatementSummary()}
              total={this.getMonthlyStatementTotal()}
              paymentDay={this.getPaymentDay()}
              openBalance={{
                value: '10.0',
                converted: '5.20'
              }}
              closingBalance={{
                value: '11.0',
                converted: '5.30'
              }}
            />
            : null
          }
      </>
    )
  }
}

const mapStateToProps = (state: Rewards.ApplicationState) => ({
  rewardsData: state.rewardsData
})

const mapDispatchToProps = (dispatch: Dispatch) => ({
  actions: bindActionCreators(rewardsActions, dispatch)
})

export default connect(
  mapStateToProps,
  mapDispatchToProps
)(PageWallet)
