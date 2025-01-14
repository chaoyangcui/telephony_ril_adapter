/*
 * Copyright (C) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "at_sim.h"
#include "vendor_adapter.h"

static int GetSimType(void)
{
    int ret;
    int simType;
    ResponseInfo *pResponse = NULL;
    char *pLine = NULL;

    ret = SendCommandLock("AT^CARDMODE", "^CARDMODE:", 0, &pResponse);
    if (pResponse == NULL) {
        TELEPHONY_LOGE("GetSimType pResponse is NULL");
        return HRIL_UNKNOWN;
    }
    if (ret != 0 || !pResponse->success) {
        TELEPHONY_LOGE("AT^CARDMODE send failed");
        return HRIL_UNKNOWN;
    }
    if (pResponse->head) {
        pLine = pResponse->head->data;
    }
    ret = SkipATPrefix(&pLine);
    if (ret < 0) {
        return HRIL_UNKNOWN;
    }
    ret = NextInt(&pLine, &simType);
    if (ret < 0) {
        return HRIL_UNKNOWN;
    }
    TELEPHONY_LOGD("simType = %{public}d", simType);
    FreeResponseInfo(pResponse);
    return simType;
}

static int GetSimState(char *pLine, char *pResult, ResponseInfo *pResponse)
{
    int status = HRIL_SIM_NOT_INSERTED;
    int ret;

    ret = SkipATPrefix(&pLine);
    if (ret != 0) {
        return HRIL_SIM_NOT_READY;
    }
    ret = NextStr(&pLine, &pResult);
    if (ret != 0) {
        return HRIL_SIM_NOT_READY;
    }
    if (strcmp(pResult, "READY") == 0) {
        status = HRIL_SIM_READY;
    } else if (strcmp(pResult, "SIM PIN") == 0) {
        status = HRIL_SIM_PIN;
    } else if (strcmp(pResult, "SIM PUK") == 0) {
        status = HRIL_SIM_PUK;
    } else if (strcmp(pResult, "PH-NET PIN") == 0) {
        status = HRIL_PH_NET_PIN;
    } else if (strcmp(pResult, "PH-NET PUK") == 0) {
        status = HRIL_PH_NET_PIN;
    } else if (!ReportStrWith(pResponse->result, "+CME ERROR:")) {
        status = HRIL_SIM_NOT_READY;
    }
    return status;
}

static int ParseSimResponseResult(char *pLine, HRilSimIOResponse *pSimResponse)
{
    int err;

    err = SkipATPrefix(&pLine);
    if (err != 0) {
        return err;
    }
    err = NextInt(&pLine, &pSimResponse->sw1);
    if (err != 0) {
        return err;
    }
    err = NextInt(&pLine, &pSimResponse->sw2);
    if (err != 0) {
        return err;
    }

    if ((pLine != NULL && *pLine != '\0')) {
        err = NextStr(&pLine, &pSimResponse->response);
        if (err != 0) {
            return err;
        }
    }
    return 0;
}

static int ParseSimPinInputTimesResult(char *pLine, HRilPinInputTimes *pinInputTimes)
{
    int err = -1;
    if (pinInputTimes == NULL) {
        TELEPHONY_LOGE("pinInputTimes is null!!!");
        return err;
    }
    err = SkipATPrefix(&pLine);
    if (err != 0) {
        return err;
    }
    err = NextStr(&pLine, &pinInputTimes->code);
    if (err != 0) {
        return err;
    }
    err = NextInt(&pLine, &pinInputTimes->times);
    err = NextInt(&pLine, &pinInputTimes->pukTimes);
    if (err != 0) {
        return err;
    }
    err = NextInt(&pLine, &pinInputTimes->pinTimes);
    if (err != 0) {
        return err;
    }
    err = NextInt(&pLine, &pinInputTimes->puk2Times);
    if (err != 0) {
        return err;
    }
    err = NextInt(&pLine, &pinInputTimes->pin2Times);
    if (err != 0) {
        return err;
    }

    return 0;
}

void ReqGetSimStatus(const ReqDataInfo *requestInfo)
{
    int ret;
    int noSimErrCode = 10;
    ResponseInfo *pResponse = NULL;
    char *pLine = NULL;
    char *pResult = NULL;
    struct ReportInfo reportInfo;
    (void)memset_s(&reportInfo, sizeof(struct ReportInfo), 0, sizeof(struct ReportInfo));
    HRilCardState cardState;
    (void)memset_s(&cardState, sizeof(cardState), 0, sizeof(cardState));
    HRilRadioState radioState = GetRadioState();

    if (radioState == HRIL_RADIO_POWER_STATE_UNAVAILABLE || radioState == HRIL_RADIO_POWER_STATE_OFF) {
        TELEPHONY_LOGD("radioState = %{public}u", radioState);
        cardState.simState = HRIL_SIM_NOT_READY;
    }

    TELEPHONY_LOGD("enter to [%{public}s]:%{public}d", __func__, __LINE__);
    cardState.simType = GetSimType();
    ret = SendCommandLock("AT+CPIN?", "+CPIN:", 0, &pResponse);
    if (ret != 0 || !pResponse->success) {
        TELEPHONY_LOGE("AT+CPIN? send failed");
        goto ERR;
    }
    if (pResponse->head) {
        pLine = pResponse->head->data;
    }
    cardState.simState = GetSimState(pLine, pResult, pResponse);
    TELEPHONY_LOGD("simType = %{public}u simState = %{public}u", cardState.simType, cardState.simState);
    reportInfo = CreateReportInfo(requestInfo, HRIL_ERR_SUCCESS, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, &cardState, sizeof(cardState));
    FreeResponseInfo(pResponse);
    return;

ERR:
    if (pResponse && pResponse->result) {
        pLine = pResponse->result;
        SkipATPrefix(&pLine);
        NextInt(&pLine, &ret);
    } else {
        ret = HRIL_ERR_GENERIC_FAILURE;
    }
    if (ret == noSimErrCode) {
        cardState.simState = HRIL_SIM_NOT_INSERTED;
        reportInfo = CreateReportInfo(requestInfo, HRIL_ERR_SUCCESS, HRIL_RESPONSE, 0);
        OnSimReport(reportInfo, &cardState, sizeof(cardState));
    } else {
        reportInfo = CreateReportInfo(requestInfo, ret, HRIL_RESPONSE, 0);
        OnSimReport(reportInfo, NULL, 0);
    }
    FreeResponseInfo(pResponse);
}

void ReqGetSimIO(const ReqDataInfo *requestInfo, const HRilSimIO *data, size_t dataLen)
{
    int ret;
    char *pLine = NULL;
    HRilSimIO *pSim = NULL;
    HRilSimIOResponse simResponse;
    char *cmd = NULL;
    ResponseInfo *pResponse = NULL;
    struct ReportInfo reportInfo;
    (void)memset_s(&reportInfo, sizeof(struct ReportInfo), 0, sizeof(struct ReportInfo));

    TELEPHONY_LOGD("enter to [%{public}s]:%{public}d  %{public}zu", __func__, __LINE__, dataLen);

    pSim = (HRilSimIO *)data;

    asprintf(&cmd, "AT+CRSM=%d,%d,%d,%d,%d,%s,\"%s\"", pSim->command, pSim->fileid, pSim->p1, pSim->p2, pSim->p3,
        (pSim->data == NULL ? "" : pSim->data), pSim->pathid);
    ret = SendCommandLock(cmd, "+CRSM", 0, &pResponse);
    free(cmd);
    if (ret != 0 || !pResponse->success) {
        TELEPHONY_LOGE("AT+CRSM send failed");
        goto ERR;
    }
    if (pResponse->head) {
        pLine = pResponse->head->data;
    }
    ret = ParseSimResponseResult(pLine, &simResponse);
    if (ret != 0) {
        goto ERR;
    }
    reportInfo = CreateReportInfo(requestInfo, HRIL_ERR_SUCCESS, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, &simResponse, sizeof(simResponse));
    FreeResponseInfo(pResponse);
    return;
ERR:
    if (pResponse && pResponse->result) {
        pLine = pResponse->result;
        SkipATPrefix(&pLine);
        NextInt(&pLine, &ret);
    } else {
        ret = HRIL_ERR_GENERIC_FAILURE;
    }
    reportInfo = CreateReportInfo(requestInfo, ret, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, NULL, 0);
    FreeResponseInfo(pResponse);
}

void ReqGetSimImsi(const ReqDataInfo *requestInfo)
{
    ResponseInfo *pResponse = NULL;
    int ret;
    char *pLine = NULL;
    struct ReportInfo reportInfo;
    (void)memset_s(&reportInfo, sizeof(struct ReportInfo), 0, sizeof(struct ReportInfo));
    char *result = NULL;

    TELEPHONY_LOGD("enter to [%{public}s]:%{public}d", __func__, __LINE__);
    ret = SendCommandLock("AT+CIMI", NULL, 0, &pResponse);
    if (ret != 0 || !pResponse->success) {
        TELEPHONY_LOGE("AT+CIMI send failed");
        goto ERR;
    }
    if (pResponse->head) {
        result = pResponse->head->data;
    } else {
        goto ERR;
    }
    reportInfo = CreateReportInfo(requestInfo, HRIL_ERR_SUCCESS, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, result, sizeof(char *));
    FreeResponseInfo(pResponse);
    return;
ERR:
    if (pResponse && pResponse->result) {
        pLine = pResponse->result;
        SkipATPrefix(&pLine);
        NextInt(&pLine, &ret);
    } else {
        ret = HRIL_ERR_GENERIC_FAILURE;
    }
    reportInfo = CreateReportInfo(requestInfo, ret, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, NULL, 0);
    FreeResponseInfo(pResponse);
}

void ReqGetSimIccID(const ReqDataInfo *requestInfo)
{
    ResponseInfo *pResponse = NULL;
    int ret;
    char *pLine = NULL;
    char *iccId = NULL;
    struct ReportInfo reportInfo;
    (void)memset_s(&reportInfo, sizeof(struct ReportInfo), 0, sizeof(struct ReportInfo));

    TELEPHONY_LOGD("enter to [%{public}s]:%{public}d", __func__, __LINE__);
    ret = SendCommandLock("AT+ICCID", "+ICCID", 0, &pResponse);
    if (ret != 0 || !pResponse->success) {
        TELEPHONY_LOGE("AT+ICCID send failed");
        goto ERR;
    }
    if (pResponse->head) {
        pLine = pResponse->head->data;
    }
    ret = SkipATPrefix(&pLine);
    if (ret != 0) {
        goto ERR;
    }

    if ((pLine != NULL && *pLine != '\0')) {
        ret = NextStr(&pLine, &iccId);
        if (ret != 0) {
            goto ERR;
        }
    }
    reportInfo = CreateReportInfo(requestInfo, HRIL_ERR_SUCCESS, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, iccId, sizeof(char *));
    FreeResponseInfo(pResponse);
    return;
ERR:
    if (pResponse && pResponse->result) {
        pLine = pResponse->result;
        SkipATPrefix(&pLine);
        NextInt(&pLine, &ret);
    } else {
        ret = HRIL_ERR_GENERIC_FAILURE;
    }
    reportInfo = CreateReportInfo(requestInfo, ret, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, NULL, 0);
    FreeResponseInfo(pResponse);
}

void ReqGetSimLockStatus(const ReqDataInfo *requestInfo, const HRilSimClock *data, size_t dataLen)
{
    ResponseInfo *pResponse = NULL;
    char *cmd = NULL;
    int ret = 0;
    char *pLine = NULL;
    int status = -1;
    HRilSimClock *pSimClck = NULL;
    struct ReportInfo reportInfo;
    (void)memset_s(&reportInfo, sizeof(struct ReportInfo), 0, sizeof(struct ReportInfo));

    pSimClck = (HRilSimClock *)data;
    asprintf(&cmd, "AT+CLCK=\"%s\",%d", pSimClck->fac, pSimClck->mode);
    ret = SendCommandLock(cmd, "+CLCK", 0, &pResponse);
    free(cmd);
    if (ret != 0 || !pResponse->success) {
        TELEPHONY_LOGE("AT+CLCK send failed  dataLen:%{public}zu", dataLen);
        goto ERR;
    }
    if (pResponse->head) {
        pLine = pResponse->head->data;
    }
    ret = SkipATPrefix(&pLine);
    if (ret != 0) {
        goto ERR;
    }

    ret = NextInt(&pLine, &status);
    if (ret != 0) {
        goto ERR;
    }

    reportInfo = CreateReportInfo(requestInfo, HRIL_ERR_SUCCESS, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, &status, sizeof(int *));
    FreeResponseInfo(pResponse);
    return;
ERR:
    if (pResponse && pResponse->result) {
        pLine = pResponse->result;
        SkipATPrefix(&pLine);
        NextInt(&pLine, &ret);
    } else {
        ret = HRIL_ERR_GENERIC_FAILURE;
    }
    reportInfo = CreateReportInfo(requestInfo, ret, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, NULL, 0);
    FreeResponseInfo(pResponse);
}

void ReqSetSimLock(const ReqDataInfo *requestInfo, const HRilSimClock *data, size_t dataLen)
{
    ResponseInfo *pResponse = NULL;
    char *cmd = NULL;
    char *pLine = NULL;
    int ret = 0;
    HRilSimClock *pSimClck = NULL;
    struct ReportInfo reportInfo;
    (void)memset_s(&reportInfo, sizeof(struct ReportInfo), 0, sizeof(struct ReportInfo));

    pSimClck = (HRilSimClock *)data;
    asprintf(&cmd, "AT+CLCK=\"%s\",%d,\"%s\"", pSimClck->fac, pSimClck->mode, pSimClck->passwd);
    ret = SendCommandLock(cmd, "+CLCK", 0, &pResponse);
    free(cmd);
    if (ret != 0 || !pResponse->success) {
        TELEPHONY_LOGE("AT+CLCK send failed  dataLen:%{public}zu", dataLen);
        goto ERR;
    }

    reportInfo = CreateReportInfo(requestInfo, HRIL_ERR_SUCCESS, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, NULL, 0);
    FreeResponseInfo(pResponse);
    return;
ERR:
    if (pResponse && pResponse->result) {
        pLine = pResponse->result;
        SkipATPrefix(&pLine);
        NextInt(&pLine, &ret);
    } else {
        ret = HRIL_ERR_GENERIC_FAILURE;
    }
    reportInfo = CreateReportInfo(requestInfo, ret, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, NULL, 0);
    FreeResponseInfo(pResponse);
}

void ReqChangeSimPassword(const ReqDataInfo *requestInfo, const HRilSimPassword *data, size_t dataLen)
{
    ResponseInfo *pResponse = NULL;
    char *cmd = NULL;
    char *pLine = NULL;
    HRilSimPassword *pSimPassword = NULL;
    int ret = -1;
    struct ReportInfo reportInfo;
    (void)memset_s(&reportInfo, sizeof(struct ReportInfo), 0, sizeof(struct ReportInfo));

    pSimPassword = (HRilSimPassword *)data;
    asprintf(&cmd, "AT+CPWD=\"%s\",\"%s\",\"%s\"", pSimPassword->fac, pSimPassword->oldPassword,
        pSimPassword->newPassword);
    ret = SendCommandLock(cmd, "+CPWD", 0, &pResponse);
    free(cmd);
    if (ret != 0 || !pResponse->success) {
        TELEPHONY_LOGE("AT+CPWD send failed  dataLen:%{public}zu", dataLen);
        goto ERR;
    }

    reportInfo = CreateReportInfo(requestInfo, HRIL_ERR_SUCCESS, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, NULL, 0);
    FreeResponseInfo(pResponse);
    return;
ERR:
    if (pResponse && pResponse->result) {
        pLine = pResponse->result;
        SkipATPrefix(&pLine);
        NextInt(&pLine, &ret);
    } else {
        ret = HRIL_ERR_GENERIC_FAILURE;
    }
    reportInfo = CreateReportInfo(requestInfo, ret, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, NULL, 0);
    FreeResponseInfo(pResponse);
}

void ReqEnterSimPin(const ReqDataInfo *requestInfo, const char *pin)
{
    ResponseInfo *pResponse = NULL;
    char *cmd = NULL;
    int ret = -1;
    char *pLine = NULL;
    struct ReportInfo reportInfo;
    (void)memset_s(&reportInfo, sizeof(struct ReportInfo), 0, sizeof(struct ReportInfo));

    asprintf(&cmd, "AT+CPIN=\"%s\"", pin);
    ret = SendCommandLock(cmd, "+CPIN", 0, &pResponse);
    free(cmd);
    if (ret != 0 || !pResponse->success) {
        TELEPHONY_LOGE("AT+CPIN send failed");
        goto ERR;
    }

    reportInfo = CreateReportInfo(requestInfo, HRIL_ERR_SUCCESS, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, NULL, 0);
    FreeResponseInfo(pResponse);
    return;
ERR:
    if (pResponse && pResponse->result) {
        pLine = pResponse->result;
        SkipATPrefix(&pLine);
        NextInt(&pLine, &ret);
    } else {
        ret = HRIL_ERR_GENERIC_FAILURE;
    }
    reportInfo = CreateReportInfo(requestInfo, ret, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, NULL, 0);
    FreeResponseInfo(pResponse);
}

void ReqUnlockSimPin(const ReqDataInfo *requestInfo, const char *puk, const char *pin)
{
    ResponseInfo *pResponse = NULL;
    char *cmd = NULL;
    char *pLine = NULL;
    int ret = -1;
    struct ReportInfo reportInfo;
    (void)memset_s(&reportInfo, sizeof(struct ReportInfo), 0, sizeof(struct ReportInfo));

    asprintf(&cmd, "AT+CPIN=\"%s\",\"%s\"", puk, pin);
    ret = SendCommandLock(cmd, "+CPIN", 0, &pResponse);
    free(cmd);
    if (ret != 0 || !pResponse->success) {
        TELEPHONY_LOGE("AT+CPIN send failed");
        goto ERR;
    }

    reportInfo = CreateReportInfo(requestInfo, HRIL_ERR_SUCCESS, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, NULL, 0);
    FreeResponseInfo(pResponse);
    return;
ERR:
    if (pResponse && pResponse->result) {
        pLine = pResponse->result;
        SkipATPrefix(&pLine);
        NextInt(&pLine, &ret);
    } else {
        ret = HRIL_ERR_GENERIC_FAILURE;
    }
    reportInfo = CreateReportInfo(requestInfo, ret, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, NULL, 0);
    FreeResponseInfo(pResponse);
}

void ReqGetSimPinInputTimes(const ReqDataInfo *requestInfo)
{
    int ret;
    char *pLine = NULL;
    HRilPinInputTimes pinInputTimes;
    ResponseInfo *pResponse = NULL;
    struct ReportInfo reportInfo;
    (void)memset_s(&reportInfo, sizeof(struct ReportInfo), 0, sizeof(struct ReportInfo));

    TELEPHONY_LOGD("enter to [%{public}s]:%{public}d", __func__, __LINE__);
    (void)memset_s(&pinInputTimes, sizeof(pinInputTimes), 0, sizeof(pinInputTimes));
    ret = SendCommandLock("AT^CPIN?", "^CPIN", 0, &pResponse);
    if (ret != 0 || !pResponse->success) {
        TELEPHONY_LOGE("AT^CPIN? send failed");
        goto ERR;
    }
    if (pResponse->head) {
        pLine = pResponse->head->data;
    }
    ret = ParseSimPinInputTimesResult(pLine, &pinInputTimes);
    if (ret != 0) {
        goto ERR;
    }
    reportInfo = CreateReportInfo(requestInfo, HRIL_ERR_SUCCESS, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, &pinInputTimes, sizeof(pinInputTimes));
    FreeResponseInfo(pResponse);
    return;
ERR:
    if (pResponse && pResponse->result) {
        pLine = pResponse->result;
        SkipATPrefix(&pLine);
        NextInt(&pLine, &ret);
    } else {
        ret = HRIL_ERR_GENERIC_FAILURE;
    }
    reportInfo = CreateReportInfo(requestInfo, ret, HRIL_RESPONSE, 0);
    OnSimReport(reportInfo, NULL, 0);
    FreeResponseInfo(pResponse);
}
