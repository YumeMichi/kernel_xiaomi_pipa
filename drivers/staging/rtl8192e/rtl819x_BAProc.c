/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 ******************************************************************************/
#include <asm/byteorder.h>
#include <linux/unaligned.h>
#include <linux/etherdevice.h>
#include "rtllib.h"
#include "rtl819x_BA.h"

static void ActivateBAEntry(struct rtllib_device *ieee, struct ba_record *pBA,
			    u16 Time)
{
	pBA->bValid = true;
	if (Time != 0)
		mod_timer(&pBA->Timer, jiffies + msecs_to_jiffies(Time));
}

static void DeActivateBAEntry(struct rtllib_device *ieee, struct ba_record *pBA)
{
	pBA->bValid = false;
	del_timer_sync(&pBA->Timer);
}

static u8 TxTsDeleteBA(struct rtllib_device *ieee, struct tx_ts_record *pTxTs)
{
	struct ba_record *pAdmittedBa = &pTxTs->TxAdmittedBARecord;
	struct ba_record *pPendingBa = &pTxTs->TxPendingBARecord;
	u8 bSendDELBA = false;

	if (pPendingBa->bValid) {
		DeActivateBAEntry(ieee, pPendingBa);
		bSendDELBA = true;
	}

	if (pAdmittedBa->bValid) {
		DeActivateBAEntry(ieee, pAdmittedBa);
		bSendDELBA = true;
	}
	return bSendDELBA;
}

static u8 RxTsDeleteBA(struct rtllib_device *ieee, struct rx_ts_record *pRxTs)
{
	struct ba_record *pBa = &pRxTs->RxAdmittedBARecord;
	u8			bSendDELBA = false;

	if (pBa->bValid) {
		DeActivateBAEntry(ieee, pBa);
		bSendDELBA = true;
	}

	return bSendDELBA;
}

void ResetBaEntry(struct ba_record *pBA)
{
	pBA->bValid			= false;
	pBA->BaParamSet.shortData	= 0;
	pBA->BaTimeoutValue		= 0;
	pBA->DialogToken		= 0;
	pBA->BaStartSeqCtrl.ShortData	= 0;
}
static struct sk_buff *rtllib_ADDBA(struct rtllib_device *ieee, u8 *Dst,
				    struct ba_record *pBA,
				    u16 StatusCode, u8 type)
{
	struct sk_buff *skb = NULL;
	struct rtllib_hdr_3addr *BAReq = NULL;
	u8 *tag = NULL;
	u16 len = ieee->tx_headroom + 9;

	netdev_dbg(ieee->dev, "%s(): frame(%d) sentd to: %pM, ieee->dev:%p\n",
		   __func__, type, Dst, ieee->dev);

	if (!pBA) {
		netdev_warn(ieee->dev, "pBA is NULL\n");
		return NULL;
	}
	skb = dev_alloc_skb(len + sizeof(struct rtllib_hdr_3addr));
	if (!skb)
		return NULL;

	memset(skb->data, 0, sizeof(struct rtllib_hdr_3addr));

	skb_reserve(skb, ieee->tx_headroom);

	BAReq = skb_put(skb, sizeof(struct rtllib_hdr_3addr));

	ether_addr_copy(BAReq->addr1, Dst);
	ether_addr_copy(BAReq->addr2, ieee->dev->dev_addr);

	ether_addr_copy(BAReq->addr3, ieee->current_network.bssid);
	BAReq->frame_ctl = cpu_to_le16(RTLLIB_STYPE_MANAGE_ACT);

	tag = skb_put(skb, 9);
	*tag++ = ACT_CAT_BA;
	*tag++ = type;
	*tag++ = pBA->DialogToken;

	if (type == ACT_ADDBARSP) {
		RT_TRACE(COMP_DBG, "====>to send ADDBARSP\n");

		put_unaligned_le16(StatusCode, tag);
		tag += 2;
	}

	put_unaligned_le16(pBA->BaParamSet.shortData, tag);
	tag += 2;

	put_unaligned_le16(pBA->BaTimeoutValue, tag);
	tag += 2;

	if (type == ACT_ADDBAREQ) {
		memcpy(tag, (u8 *)&(pBA->BaStartSeqCtrl), 2);
		tag += 2;
	}

#ifdef VERBOSE_DEBUG
	print_hex_dump_bytes("rtllib_ADDBA(): ", DUMP_PREFIX_NONE, skb->data,
			     skb->len);
#endif
	return skb;
}

static struct sk_buff *rtllib_DELBA(struct rtllib_device *ieee, u8 *dst,
				    struct ba_record *pBA,
				    enum tr_select TxRxSelect, u16 ReasonCode)
{
	union delba_param_set DelbaParamSet;
	struct sk_buff *skb = NULL;
	 struct rtllib_hdr_3addr *Delba = NULL;
	u8 *tag = NULL;
	u16 len = 6 + ieee->tx_headroom;

	if (net_ratelimit())
		netdev_dbg(ieee->dev, "%s(): ReasonCode(%d) sentd to: %pM\n",
			   __func__, ReasonCode, dst);

	memset(&DelbaParamSet, 0, 2);

	DelbaParamSet.field.Initiator = (TxRxSelect == TX_DIR) ? 1 : 0;
	DelbaParamSet.field.TID	= pBA->BaParamSet.field.TID;

	skb = dev_alloc_skb(len + sizeof(struct rtllib_hdr_3addr));
	if (!skb)
		return NULL;

	skb_reserve(skb, ieee->tx_headroom);

	Delba = skb_put(skb, sizeof(struct rtllib_hdr_3addr));

	ether_addr_copy(Delba->addr1, dst);
	ether_addr_copy(Delba->addr2, ieee->dev->dev_addr);
	ether_addr_copy(Delba->addr3, ieee->current_network.bssid);
	Delba->frame_ctl = cpu_to_le16(RTLLIB_STYPE_MANAGE_ACT);

	tag = skb_put(skb, 6);

	*tag++ = ACT_CAT_BA;
	*tag++ = ACT_DELBA;


	put_unaligned_le16(DelbaParamSet.shortData, tag);
	tag += 2;

	put_unaligned_le16(ReasonCode, tag);
	tag += 2;

#ifdef VERBOSE_DEBUG
	print_hex_dump_bytes("rtllib_DELBA(): ", DUMP_PREFIX_NONE, skb->data,
			     skb->len);
#endif
	return skb;
}

static void rtllib_send_ADDBAReq(struct rtllib_device *ieee, u8 *dst,
				 struct ba_record *pBA)
{
	struct sk_buff *skb;

	skb = rtllib_ADDBA(ieee, dst, pBA, 0, ACT_ADDBAREQ);

	if (skb) {
		RT_TRACE(COMP_DBG, "====>to send ADDBAREQ!!!!!\n");
		softmac_mgmt_xmit(skb, ieee);
	} else {
		netdev_dbg(ieee->dev, "Failed to generate ADDBAReq packet.\n");
	}
}

static void rtllib_send_ADDBARsp(struct rtllib_device *ieee, u8 *dst,
				 struct ba_record *pBA, u16 StatusCode)
{
	struct sk_buff *skb;

	skb = rtllib_ADDBA(ieee, dst, pBA, StatusCode, ACT_ADDBARSP);
	if (skb)
		softmac_mgmt_xmit(skb, ieee);
	else
		netdev_dbg(ieee->dev, "Failed to generate ADDBARsp packet.\n");
}

static void rtllib_send_DELBA(struct rtllib_device *ieee, u8 *dst,
			      struct ba_record *pBA, enum tr_select TxRxSelect,
			      u16 ReasonCode)
{
	struct sk_buff *skb;

	skb = rtllib_DELBA(ieee, dst, pBA, TxRxSelect, ReasonCode);
	if (skb)
		softmac_mgmt_xmit(skb, ieee);
	else
		netdev_dbg(ieee->dev, "Failed to generate DELBA packet.\n");
}

int rtllib_rx_ADDBAReq(struct rtllib_device *ieee, struct sk_buff *skb)
{
	struct rtllib_hdr_3addr *req = NULL;
	u16 rc = 0;
	u8 *dst = NULL, *pDialogToken = NULL, *tag = NULL;
	struct ba_record *pBA = NULL;
	union ba_param_set *pBaParamSet = NULL;
	u16 *pBaTimeoutVal = NULL;
	union sequence_control *pBaStartSeqCtrl = NULL;
	struct rx_ts_record *pTS = NULL;

	if (skb->len < sizeof(struct rtllib_hdr_3addr) + 9) {
		netdev_warn(ieee->dev, "Invalid skb len in BAREQ(%d / %d)\n",
			    (int)skb->len,
			    (int)(sizeof(struct rtllib_hdr_3addr) + 9));
		return -1;
	}

#ifdef VERBOSE_DEBUG
	print_hex_dump_bytes("rtllib_rx_ADDBAReq(): ", DUMP_PREFIX_NONE,
			     skb->data, skb->len);
#endif

	req = (struct rtllib_hdr_3addr *) skb->data;
	tag = (u8 *)req;
	dst = (u8 *)(&req->addr2[0]);
	tag += sizeof(struct rtllib_hdr_3addr);
	pDialogToken = tag + 2;
	pBaParamSet = (union ba_param_set *)(tag + 3);
	pBaTimeoutVal = (u16 *)(tag + 5);
	pBaStartSeqCtrl = (union sequence_control *)(req + 7);

	RT_TRACE(COMP_DBG, "====>rx ADDBAREQ from : %pM\n", dst);
	if (!ieee->current_network.qos_data.active ||
	    !ieee->pHTInfo->bCurrentHTSupport ||
	    (ieee->pHTInfo->IOTAction & HT_IOT_ACT_REJECT_ADDBA_REQ)) {
		rc = ADDBA_STATUS_REFUSED;
		netdev_warn(ieee->dev,
			    "Failed to reply on ADDBA_REQ as some capability is not ready(%d, %d)\n",
			    ieee->current_network.qos_data.active,
			    ieee->pHTInfo->bCurrentHTSupport);
		goto OnADDBAReq_Fail;
	}
	if (!GetTs(ieee, (struct ts_common_info **)(&pTS), dst,
	    (u8)(pBaParamSet->field.TID), RX_DIR, true)) {
		rc = ADDBA_STATUS_REFUSED;
		netdev_warn(ieee->dev, "%s(): can't get TS\n", __func__);
		goto OnADDBAReq_Fail;
	}
	pBA = &pTS->RxAdmittedBARecord;

	if (pBaParamSet->field.BAPolicy == BA_POLICY_DELAYED) {
		rc = ADDBA_STATUS_INVALID_PARAM;
		netdev_warn(ieee->dev, "%s(): BA Policy is not correct\n",
			    __func__);
		goto OnADDBAReq_Fail;
	}

	rtllib_FlushRxTsPendingPkts(ieee, pTS);

	DeActivateBAEntry(ieee, pBA);
	pBA->DialogToken = *pDialogToken;
	pBA->BaParamSet = *pBaParamSet;
	pBA->BaTimeoutValue = *pBaTimeoutVal;
	pBA->BaStartSeqCtrl = *pBaStartSeqCtrl;

	if (ieee->GetHalfNmodeSupportByAPsHandler(ieee->dev) ||
	   (ieee->pHTInfo->IOTAction & HT_IOT_ACT_ALLOW_PEER_AGG_ONE_PKT))
		pBA->BaParamSet.field.BufferSize = 1;
	else
		pBA->BaParamSet.field.BufferSize = 32;

	ActivateBAEntry(ieee, pBA, 0);
	rtllib_send_ADDBARsp(ieee, dst, pBA, ADDBA_STATUS_SUCCESS);

	return 0;

OnADDBAReq_Fail:
	{
		struct ba_record BA;

		BA.BaParamSet = *pBaParamSet;
		BA.BaTimeoutValue = *pBaTimeoutVal;
		BA.DialogToken = *pDialogToken;
		BA.BaParamSet.field.BAPolicy = BA_POLICY_IMMEDIATE;
		rtllib_send_ADDBARsp(ieee, dst, &BA, rc);
		return 0;
	}
}

int rtllib_rx_ADDBARsp(struct rtllib_device *ieee, struct sk_buff *skb)
{
	 struct rtllib_hdr_3addr *rsp = NULL;
	struct ba_record *pPendingBA, *pAdmittedBA;
	struct tx_ts_record *pTS = NULL;
	u8 *dst = NULL, *pDialogToken = NULL, *tag = NULL;
	u16 *pStatusCode = NULL, *pBaTimeoutVal = NULL;
	union ba_param_set *pBaParamSet = NULL;
	u16			ReasonCode;

	if (skb->len < sizeof(struct rtllib_hdr_3addr) + 9) {
		netdev_warn(ieee->dev, "Invalid skb len in BARSP(%d / %d)\n",
			    (int)skb->len,
			    (int)(sizeof(struct rtllib_hdr_3addr) + 9));
		return -1;
	}
	rsp = (struct rtllib_hdr_3addr *)skb->data;
	tag = (u8 *)rsp;
	dst = (u8 *)(&rsp->addr2[0]);
	tag += sizeof(struct rtllib_hdr_3addr);
	pDialogToken = tag + 2;
	pStatusCode = (u16 *)(tag + 3);
	pBaParamSet = (union ba_param_set *)(tag + 5);
	pBaTimeoutVal = (u16 *)(tag + 7);

	RT_TRACE(COMP_DBG, "====>rx ADDBARSP from : %pM\n", dst);
	if (!ieee->current_network.qos_data.active ||
	    !ieee->pHTInfo->bCurrentHTSupport ||
	    !ieee->pHTInfo->bCurrentAMPDUEnable) {
		netdev_warn(ieee->dev,
			    "reject to ADDBA_RSP as some capability is not ready(%d, %d, %d)\n",
			    ieee->current_network.qos_data.active,
			    ieee->pHTInfo->bCurrentHTSupport,
			    ieee->pHTInfo->bCurrentAMPDUEnable);
		ReasonCode = DELBA_REASON_UNKNOWN_BA;
		goto OnADDBARsp_Reject;
	}


	if (!GetTs(ieee, (struct ts_common_info **)(&pTS), dst,
		   (u8)(pBaParamSet->field.TID), TX_DIR, false)) {
		netdev_warn(ieee->dev, "%s(): can't get TS\n", __func__);
		ReasonCode = DELBA_REASON_UNKNOWN_BA;
		goto OnADDBARsp_Reject;
	}

	pTS->bAddBaReqInProgress = false;
	pPendingBA = &pTS->TxPendingBARecord;
	pAdmittedBA = &pTS->TxAdmittedBARecord;


	if (pAdmittedBA->bValid) {
		netdev_dbg(ieee->dev, "%s(): ADDBA response already admitted\n",
			   __func__);
		return -1;
	} else if (!pPendingBA->bValid ||
		   (*pDialogToken != pPendingBA->DialogToken)) {
		netdev_warn(ieee->dev,
			    "%s(): ADDBA Rsp. BA invalid, DELBA!\n",
			    __func__);
		ReasonCode = DELBA_REASON_UNKNOWN_BA;
		goto OnADDBARsp_Reject;
	} else {
		netdev_dbg(ieee->dev,
			   "%s(): Recv ADDBA Rsp. BA is admitted! Status code:%X\n",
			   __func__, *pStatusCode);
		DeActivateBAEntry(ieee, pPendingBA);
	}


	if (*pStatusCode == ADDBA_STATUS_SUCCESS) {
		if (pBaParamSet->field.BAPolicy == BA_POLICY_DELAYED) {
			pTS->bAddBaReqDelayed = true;
			DeActivateBAEntry(ieee, pAdmittedBA);
			ReasonCode = DELBA_REASON_END_BA;
			goto OnADDBARsp_Reject;
		}


		pAdmittedBA->DialogToken = *pDialogToken;
		pAdmittedBA->BaTimeoutValue = *pBaTimeoutVal;
		pAdmittedBA->BaStartSeqCtrl = pPendingBA->BaStartSeqCtrl;
		pAdmittedBA->BaParamSet = *pBaParamSet;
		DeActivateBAEntry(ieee, pAdmittedBA);
		ActivateBAEntry(ieee, pAdmittedBA, *pBaTimeoutVal);
	} else {
		pTS->bAddBaReqDelayed = true;
		pTS->bDisable_AddBa = true;
		ReasonCode = DELBA_REASON_END_BA;
		goto OnADDBARsp_Reject;
	}

	return 0;

OnADDBARsp_Reject:
	{
		struct ba_record BA;

		BA.BaParamSet = *pBaParamSet;
		rtllib_send_DELBA(ieee, dst, &BA, TX_DIR, ReasonCode);
		return 0;
	}
}

int rtllib_rx_DELBA(struct rtllib_device *ieee, struct sk_buff *skb)
{
	 struct rtllib_hdr_3addr *delba = NULL;
	union delba_param_set *pDelBaParamSet = NULL;
	u8 *dst = NULL;

	if (skb->len < sizeof(struct rtllib_hdr_3addr) + 6) {
		netdev_warn(ieee->dev, "Invalid skb len in DELBA(%d / %d)\n",
			    (int)skb->len,
			    (int)(sizeof(struct rtllib_hdr_3addr) + 6));
		return -1;
	}

	if (!ieee->current_network.qos_data.active ||
		!ieee->pHTInfo->bCurrentHTSupport) {
		netdev_warn(ieee->dev,
			    "received DELBA while QOS or HT is not supported(%d, %d)\n",
			    ieee->current_network. qos_data.active,
			    ieee->pHTInfo->bCurrentHTSupport);
		return -1;
	}

#ifdef VERBOSE_DEBUG
	print_hex_dump_bytes("rtllib_rx_DELBA(): ", DUMP_PREFIX_NONE, skb->data,
			     skb->len);
#endif
	delba = (struct rtllib_hdr_3addr *)skb->data;
	dst = (u8 *)(&delba->addr2[0]);
	pDelBaParamSet = (union delba_param_set *)&delba->payload[2];

	if (pDelBaParamSet->field.Initiator == 1) {
		struct rx_ts_record *pRxTs;

		if (!GetTs(ieee, (struct ts_common_info **)&pRxTs, dst,
		    (u8)pDelBaParamSet->field.TID, RX_DIR, false)) {
			netdev_warn(ieee->dev,
				    "%s(): can't get TS for RXTS. dst:%pM TID:%d\n",
				    __func__, dst,
				    (u8)pDelBaParamSet->field.TID);
			return -1;
		}

		RxTsDeleteBA(ieee, pRxTs);
	} else {
		struct tx_ts_record *pTxTs;

		if (!GetTs(ieee, (struct ts_common_info **)&pTxTs, dst,
			   (u8)pDelBaParamSet->field.TID, TX_DIR, false)) {
			netdev_warn(ieee->dev, "%s(): can't get TS for TXTS\n",
				    __func__);
			return -1;
		}

		pTxTs->bUsingBa = false;
		pTxTs->bAddBaReqInProgress = false;
		pTxTs->bAddBaReqDelayed = false;
		del_timer_sync(&pTxTs->TsAddBaTimer);
		TxTsDeleteBA(ieee, pTxTs);
	}
	return 0;
}

void TsInitAddBA(struct rtllib_device *ieee, struct tx_ts_record *pTS,
		 u8 Policy, u8	bOverwritePending)
{
	struct ba_record *pBA = &pTS->TxPendingBARecord;

	if (pBA->bValid && !bOverwritePending)
		return;

	DeActivateBAEntry(ieee, pBA);

	pBA->DialogToken++;
	pBA->BaParamSet.field.AMSDU_Support = 0;
	pBA->BaParamSet.field.BAPolicy = Policy;
	pBA->BaParamSet.field.TID =
			 pTS->TsCommonInfo.TSpec.f.TSInfo.field.ucTSID;
	pBA->BaParamSet.field.BufferSize = 32;
	pBA->BaTimeoutValue = 0;
	pBA->BaStartSeqCtrl.field.SeqNum = (pTS->TxCurSeq + 3) % 4096;

	ActivateBAEntry(ieee, pBA, BA_SETUP_TIMEOUT);

	rtllib_send_ADDBAReq(ieee, pTS->TsCommonInfo.Addr, pBA);
}

void TsInitDelBA(struct rtllib_device *ieee,
		 struct ts_common_info *pTsCommonInfo,
		 enum tr_select TxRxSelect)
{
	if (TxRxSelect == TX_DIR) {
		struct tx_ts_record *pTxTs =
			 (struct tx_ts_record *)pTsCommonInfo;

		if (TxTsDeleteBA(ieee, pTxTs))
			rtllib_send_DELBA(ieee, pTsCommonInfo->Addr,
					  (pTxTs->TxAdmittedBARecord.bValid) ?
					 (&pTxTs->TxAdmittedBARecord) :
					(&pTxTs->TxPendingBARecord),
					 TxRxSelect, DELBA_REASON_END_BA);
	} else if (TxRxSelect == RX_DIR) {
		struct rx_ts_record *pRxTs =
				 (struct rx_ts_record *)pTsCommonInfo;
		if (RxTsDeleteBA(ieee, pRxTs))
			rtllib_send_DELBA(ieee, pTsCommonInfo->Addr,
					  &pRxTs->RxAdmittedBARecord,
					  TxRxSelect, DELBA_REASON_END_BA);
	}
}

void BaSetupTimeOut(struct timer_list *t)
{
	struct tx_ts_record *pTxTs = from_timer(pTxTs, t,
					      TxPendingBARecord.Timer);

	pTxTs->bAddBaReqInProgress = false;
	pTxTs->bAddBaReqDelayed = true;
	pTxTs->TxPendingBARecord.bValid = false;
}

void TxBaInactTimeout(struct timer_list *t)
{
	struct tx_ts_record *pTxTs = from_timer(pTxTs, t,
					      TxAdmittedBARecord.Timer);
	struct rtllib_device *ieee = container_of(pTxTs, struct rtllib_device,
				     TxTsRecord[pTxTs->num]);
	TxTsDeleteBA(ieee, pTxTs);
	rtllib_send_DELBA(ieee, pTxTs->TsCommonInfo.Addr,
			  &pTxTs->TxAdmittedBARecord, TX_DIR,
			  DELBA_REASON_TIMEOUT);
}

void RxBaInactTimeout(struct timer_list *t)
{
	struct rx_ts_record *pRxTs = from_timer(pRxTs, t,
					      RxAdmittedBARecord.Timer);
	struct rtllib_device *ieee = container_of(pRxTs, struct rtllib_device,
				     RxTsRecord[pRxTs->num]);

	RxTsDeleteBA(ieee, pRxTs);
	rtllib_send_DELBA(ieee, pRxTs->TsCommonInfo.Addr,
			  &pRxTs->RxAdmittedBARecord, RX_DIR,
			  DELBA_REASON_TIMEOUT);
}
