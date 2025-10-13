/*
 *   BSD LICENSE
 * 
 *   Copyright(c) 2007-2023 Intel Corporation. All rights reserved.
 *   All rights reserved.
 * 
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 * 
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 *  version: QAT20.L.1.2.30-00090
 */

/**
 *****************************************************************************
 * @file icp_qat_fw_init_admin.h
 * @defgroup icp_qat_fw_init_admin ICP QAT FW Initialisation/Admin Interface
 *                                 Definitions
 * @ingroup icp_qat_fw
 *
 * @description
 *      This file documents structs used at init time in the configuration of
 *      the QAT FW, as well as for Admin requests and responses.
 *
 *****************************************************************************/

#ifndef _ICP_QAT_FW_INIT_ADMIN_H_
#define _ICP_QAT_FW_INIT_ADMIN_H_

/*
******************************************************************************
* Include local header files
******************************************************************************
*/

#include "icp_qat_fw.h"

#define ICP_QAT_NUM_THREADS 8
/**
 *****************************************************************************
 * @ingroup icp_qat_fw_init_admin
 *      Definition of the init time and admin command types
 * @description
 *      Enumeration which is used to indicate the ids of Init/Admin commands
 *
 *****************************************************************************/
typedef enum
{
    ICP_QAT_FW_INIT_ME = 0,
    /**< ME Initialisation command type */

    ICP_QAT_FW_TRNG_ENABLE = 1,
    /**< TRNG Enable command type */

    ICP_QAT_FW_TRNG_DISABLE = 2,
    /**< TRNG Disable command type */

    ICP_QAT_FW_CONSTANTS_CFG = 3,
    /**< Constants configuration command type */

    ICP_QAT_FW_STATUS_GET = 4,
    /**< Admin: Status Get command type */

    ICP_QAT_FW_COUNTERS_GET = 5,
    /**< Admin: Counters Get command type */

    ICP_QAT_FW_LOOPBACK = 6,
    /**< Admin: Loopback command type */

    ICP_QAT_FW_HEARTBEAT_SYNC = 7,
    /**< Admin: Heartbeat Sync command type */

    ICP_QAT_FW_HEARTBEAT_GET = 8,
    /**< Admin: Heartbeat Get command type */

    ICP_QAT_FW_COMP_CAPABILITY_GET = 9,
    /**< Admin: Compression Capability Get command type */

    ICP_QAT_FW_CRYPTO_CAPABILITY_GET = 10,
    /**< Admin: Compression Crypto Get command type */

    ICP_QAT_FW_HEARTBEAT_CONFIG_WR = 13
    /** Admin: Heartbeat Config Update command type */

} icp_qat_fw_init_admin_cmd_id_t;

/**
 *****************************************************************************
 * @ingroup icp_qat_fw_init_admin
 *      Definition of Init/Admin Response status
 * @description
 *      Enumeration which is used to indicate the possible values of the status
 *      field within an Init/Admin Response message.
 *
 *****************************************************************************/
typedef enum {
    ICP_QAT_FW_INIT_RESP_STATUS_SUCCESS = 0,
    /**< ME Initialisation/Admin response indication successful status */

    ICP_QAT_FW_INIT_RESP_STATUS_FAIL = 1,
    /**< ME Initialisation/Admin response indication failure status */

    ICP_QAT_FW_INIT_RESP_STATUS_UNSUPPORTED = 4
    /**< ME Initialisation/Admin response indication failure status */

} icp_qat_fw_init_admin_resp_status_t;

/**
 *****************************************************************************
 * @ingroup icp_qat_fw_init_admin
 *      QAT FW Init/Admin request message
 * @description
 *      This struct contains data needed to generate a init/admin request
 *
 *****************************************************************************/
typedef struct icp_qat_fw_init_admin_req_s
{
    /**< LW0 */
    uint16_t init_cfg_sz;
    /**< Initialisation config size */

    uint8_t resrvd1;
    /**< Reserved field */

    uint8_t cmd_id;
    /**< Init/Admin time command that is described in the request */

    /**< LW1 */
    union {
        uint32_t resrvd2;
        /**< Reserved field -to keep 64-bit ptr alignment */
        uint32_t max_req_duration;
        /**< Max request execution duration in timestamp ticks(16 ME cycles)
         * For example, max_req_duration = (5-DEVIATION)*ME_FREQUENCY/16
         * DEVIATION is 300ms */
    } lw1;

    /**< LWs 2-3 */
    uint64_t opaque_data;

    /**< LWs 4-5 */
    uint64_t init_cfg_ptr;
    /**< Pointer to configuration data */

    /**< LW 6 */
    union {
        /** ICP_QAT_FW_INIT_ME */
        struct
        {
            uint16_t ibuf_size_in_kb;
            /**< Size in KB of internal buffers used to optimize dynamic
             * compression, or 0 to select the default */

            uint16_t resrvd3;
            /**< Reserved */
        } init_me;

        /** ICP_QAT_FW_HEARTBEAT_TIMER_SET */
        uint32_t heartbeat_ticks;
    } lw6;

    /**< LW 7 */
    uint32_t resrvd4;
    /**< Reserved */

} icp_qat_fw_init_admin_req_t;

/**
 *****************************************************************************
 * @ingroup icp_qat_fw_init_admin
 *      QAT FW Init/Admin response header
 * @description
 *      Structure containing the data for the Initialisation/Admin Response
 *      message header.
 *
 *****************************************************************************/
typedef struct icp_qat_fw_init_admin_resp_hdr_s
{
    /**< LW0 */
    uint8_t flags;
    /**< Flags field */

    uint8_t resrvd1;
    /**< Reserved field */

    uint8_t status;
    /**< Status field */

    uint8_t init_admin_cmd_id;
    /**< Init/Admin time command that is described in the request */

} icp_qat_fw_init_admin_resp_hdr_t;

/**
 *****************************************************************************
 * @ingroup icp_qat_fw_init_admin
 *      QAT FW Init/Admin response header for fw capability
 * @description
 *      Structure containing the data for the Initialisation/Admin Response
 *      message header.
 *
 *****************************************************************************/
typedef struct icp_qat_fw_init_admin_fw_capability_resp_hdr_s
{
    /**< LW0 */
    uint16_t reserved;
    /**< Reserved */

    uint8_t status;
    /**< Status field */

    uint8_t init_admin_cmd_id;
    /**< Init/Admin time command that is described in the request */

} icp_qat_fw_init_admin_fw_capability_resp_hdr_t;

/**
 *****************************************************************************
 * @ingroup icp_qat_fw_init_admin
 *      QAT FW Init/Admin response for fw capability compression and crypto
 * @description
 *      This struct contains data needed to generate a init/admin response
 *
 *****************************************************************************/
typedef struct icp_qat_fw_init_admin_capability_resp_s
{
    /**< LW0 */
    icp_qat_fw_init_admin_fw_capability_resp_hdr_t init_resp_hdr;
    /**< Initialisation/Admin response header */

    /**< LW1 */
    uint32_t extended_features;
    /**< Extended feature  field */

    /**< LWs 2-3 */
    uint64_t opaque_data;

    /**< LWs 4-7 */
    union {
        /**< Specific to a compression capability response only */
        struct
        {
            uint16_t compression_algos;
            /**< QAT FW supported Compression algorithms */

            uint16_t checksum_algos;
            /**< QAT FW supported Checksum algorithms */

            uint32_t deflate_capabilities;
            /**< QAT FW supported Deflate capabilities */

            uint32_t resrvd1;
            /**< Reserved field */

            uint32_t deprecated;
            /**< Deprecated field */

        } compression;

        /**< Specific to a crypto capability response only */
        struct
        {
            uint32_t cipher_algos;
            /**< QAT FW supported Cipher algorithms */

            uint32_t hash_algos;
            /**< QAT FW supported Hash algorithms */

            uint16_t keygen_algos;
            /**< QAT FW supported Key Generation algorithms */

            uint16_t other;
            /**< QAT FW other capabilities */

            uint16_t public_key_algos;
            /**< QAT FW supported Public Key algorithms */

            uint16_t prime_algos;
            /**< QAT FW supported Prime algorithms */

        } crypto;
    };

} icp_qat_fw_init_admin_capability_resp_t;

/**
 *****************************************************************************
 * @ingroup icp_qat_fw_init_admin
 *      QAT FW Init/Admin response Parameters
 * @description
 *      Structure containing the data for the Initialisation/Admin Response
 *      message Parameters field.
 *
 *****************************************************************************/
typedef struct icp_qat_fw_init_admin_resp_pars_s
{
    /**< LWs 4-7 */
    union {
        uint32_t resrvd1[ICP_QAT_FW_NUM_LONGWORDS_4];
        /**< Reserved fields - unused by all init admin common responses */

        /**< Specific to an Admin Status Get Response only */
        struct
        {
            uint32_t version_patch_num;
            /**< QAT FW build patch number */

            uint8_t context_id;
            /**< Context id of the context that serviced the status request */

            uint8_t ae_id;
            /**< id of the acceleration engine that serviced the status request
             */

            uint16_t resrvd1;
            /**< Reserved field */

            uint64_t resrvd2;
            /**< Now a reserved field */

        } s1;

        /**< Specific to an Admin Counters Get Response only */
        struct
        {
            uint64_t req_rec_count;
            /**< Request received count */

            uint64_t resp_sent_count;
            /**< Response sent count */

        } s2;

    } u;

} icp_qat_fw_init_admin_resp_pars_t;

/* ========================================================================= */
/*                              HEARTBEAT MACROS                             */
/* ========================================================================= */
/**
 *****************************************************************************
 * @ingroup icp_qat_fw_init_admin
 *      QAT FW Init/Admin response for heartbeat counter response
 * @description
 *      This struct contains data needed to generate a init/admin response
 *
 *****************************************************************************/
typedef struct icp_qat_fw_init_admin_hb_cnt_s
{
    uint16_t req_heartbeat_cnt;
    /**< Heartbeat request count */

    uint16_t resp_heartbeat_cnt;
    /**< Heartbeat response count */
} icp_qat_fw_init_admin_hb_cnt_t;

typedef struct icp_qat_fw_init_admin_hb_stats_s
{
    icp_qat_fw_init_admin_hb_cnt_t stats[ICP_QAT_NUM_THREADS];

} icp_qat_fw_init_admin_hb_stats_t;

/**
 *****************************************************************************
 * @ingroup icp_qat_fw_init_admin
 *      QAT FW Init/Admin response
 * @description
 *      This struct contains data needed to generate a init/admin response
 *
 *****************************************************************************/
typedef struct icp_qat_fw_init_admin_resp_s
{
    /**< LW0 */
    icp_qat_fw_init_admin_resp_hdr_t init_resp_hdr;
    /**< Initialisation/Admin response header */

    /**< LW1 */
    union {
        uint32_t resrvd2;
        /**< Reserved field - to keep 64-bit ptr alignment
         * - specific to all init common responses */

        /**< Specific to an Admin Status Get response only */
        struct
        {
            uint16_t version_minor_num;
            /**< QAT FW minor build number */

            uint16_t version_major_num;
            /**< QAT FW major build number */

        } s;

    } u;

    /**< LWs 2-3 */
    uint64_t opaque_data;

    /**< LWs 4-7 */
    icp_qat_fw_init_admin_resp_pars_t init_resp_pars;
    /**< Initialisation/Admin response parameters */

} icp_qat_fw_init_admin_resp_t;

/**< @ingroup icp_qat_fw_init_admin
 *  Definition of the setting of the Init-Admin response
    heartbeat flag to OK */
#define ICP_QAT_FW_COMN_HEARTBEAT_OK 0
/**< @ingroup icp_qat_fw_init_admin
 *  Definition of the setting of the Init-Admin response
    heartbeat flag to BLOCKED */
#define ICP_QAT_FW_COMN_HEARTBEAT_BLOCKED 1

/**< @ingroup icp_qat_fw_init_admin
 * Macros defining the bit position and mask of the Init-Admin response
   heartbeat flag within the flags field */
#define ICP_QAT_FW_COMN_HEARTBEAT_FLAG_BITPOS 0
#define ICP_QAT_FW_COMN_HEARTBEAT_FLAG_MASK 0x1

/**< @ingroup icp_qat_fw_init_admin
 *  Definition of the setting of the Init-Admin response
    FW TYPE LA */
#define ICP_QAT_FW_COMN_FWTYPE_LA 0x0

/**< @ingroup icp_qat_fw_init_admin
 *  Definition of the setting of the Init-Admin response
    FW TYPE DC */
#define ICP_QAT_FW_COMN_FWTYPE_DC 0x1

/**< @ingroup icp_qat_fw_init_admin
 *  Definition of the setting of the Init-Admin response
    FW TYPE DC */
#define ICP_QAT_FW_COMN_FWTYPE_INLINE 0x2

/**< @ingroup icp_qat_fw_init_admin
 *  Definition of the setting of the Init-Admin response
    FW TYPE FLAG BITPOS */
#define ICP_QAT_FW_COMN_FWTYPE_FLAG_BITPOS 0

/**< @ingroup icp_qat_fw_init_admin
 *  Definition of the setting of the Init-Admin response
    FW TYPE FLAG MASK */
#define ICP_QAT_FW_COMN_FWTYPE_FLAG_MASK 0x3

#define ICP_QAT_FW_COMN_STATUS_RESRVD_FLD_MASK 0xFE

/**
 ******************************************************************************
 * @ingroup icp_qat_fw_init_admin
 *
 * @description
 *      Extract the heartbeat flag from the Init-Admin response header
 *      structure.
 *
 * @param hdr  Response header structure 'icp_qat_fw_init_admin_resp_hdr_t'.
 *
 *****************************************************************************/
#define ICP_QAT_FW_COMN_HEARTBEAT_HDR_FLAG_GET(hdr)                            \
    ICP_QAT_FW_COMN_HEARTBEAT_FLAG_GET(hdr.flags)

/**
 ******************************************************************************
 * @ingroup icp_qat_fw_init_admin
 *
 * @description
 *      Extract the heartbeat flag from the Init-Admin response header
 *      structure.
 *
 * @param hdr  Response header structure 'icp_qat_fw_init_admin_resp_hdr_t'.
 *               Value of the heartbeat flag.
 *
 *****************************************************************************/
#define ICP_QAT_FW_COMN_HEARTBEAT_HDR_FLAG_SET(hdr, val)                       \
    ICP_QAT_FW_COMN_HEARTBEAT_FLAG_SET(hdr, val)

/**
 ******************************************************************************
 * @ingroup icp_qat_fw_init_admin
 *
 * @description
 *      Extract the heartbeat flag from the Init-Admin response header structure
 *      status field.
 *
 * @param hdr_t  Status flags field.
 *
 *****************************************************************************/
#define ICP_QAT_FW_COMN_HEARTBEAT_FLAG_GET(flags)                              \
    QAT_FIELD_GET(flags,                                                       \
                  ICP_QAT_FW_COMN_HEARTBEAT_FLAG_BITPOS,                       \
                  ICP_QAT_FW_COMN_HEARTBEAT_FLAG_MASK)

/**
 ******************************************************************************
 * @ingroup icp_qat_fw_init_admin
 *
 * @description
 *      Extract the FW type flag from the Init-Admin response header structure
 *      status field.
 *
 * @param hdr_t  Status flags field.
 *
 *****************************************************************************/
#define ICP_QAT_FW_COMN_FWTYPE_FLAG_GET(flags)                                 \
    QAT_FIELD_GET(flags,                                                       \
                  ICP_QAT_FW_COMN_FWTYPE_FLAG_BITPOS,                          \
                  ICP_QAT_FW_COMN_FWTYPE_FLAG_MASK)

/* ========================================================================= */
/*                           FW CAPABILITY DEFINES                           */
/* ========================================================================= */

/* Compression */
#define ICP_QAT_FW_CAP_COMP_EXT_CNV_BITPOS 0
#define ICP_QAT_FW_CAP_COMP_EXT_BNP_BITPOS 1
#define ICP_QAT_FW_CAP_COMP_EXT_CNV_RECOVERY_BITPOS 8
#define ICP_QAT_FW_CAP_COMP_EXT_E2E_INTEGRITY_BITPOS 9

#define ICP_QAT_FW_CAP_COMP_COMPRESSION_DEFLATE_BITPOS 0

#define ICP_QAT_FW_CAP_COMP_CHECKSUM_CRC32_BITPOS 0
#define ICP_QAT_FW_CAP_COMP_CHECKSUM_ADLER_BITPOS 1

#define ICP_QAT_FW_CAP_COMP_DEFLATE_COMPRESS_BITPOS 0
#define ICP_QAT_FW_CAP_COMP_DEFLATE_DECOMPRESS_BITPOS 1
#define ICP_QAT_FW_CAP_COMP_DEFLATE_STATEFUL_BITPOS 2
#define ICP_QAT_FW_CAP_COMP_DEFLATE_STATELESS_BITPOS 3
#define ICP_QAT_FW_CAP_COMP_DEFLATE_DYNAMIC_HUFFMAN_BITPOS 8
#define ICP_QAT_FW_CAP_COMP_DEFLATE_PRECOMP_HUFFMAN_BITPOS 9
#define ICP_QAT_FW_CAP_COMP_DEFLATE_DYN_HUFFMAN_BUFFER_BITPOS 10
#define ICP_QAT_FW_CAP_COMP_DEFLATE_AUTO_SELECT_BEST_BITPOS 11
#define ICP_QAT_FW_CAP_COMP_DEFLATE_END_OF_LAST_BLOCK_BITPOS 12

/* Cryptography */
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_NULL_BITPOS 0
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_ARC4_BITPOS 1
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_AES_ECB_BITPOS 2
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_AES_CBC_BITPOS 3
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_AES_CTR_BITPOS 4
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_AES_CCM_BITPOS 5
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_AES_GCM_BITPOS 6
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_DES_ECB_BITPOS 7
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_DES_CBC_BITPOS 8
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_3DES_ECB_BITPOS 9
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_3DES_CBC_BITPOS 10
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_3DES_CTR_BITPOS 11
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_KASUMI_F8_BITPOS 12
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_SNOW3G_UEA2_BITPOS 13
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_AES_F8_BITPOS 14
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_AES_XTS_BITPOS 15
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_ZUC_EEA3_BITPOS 16
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_CHACHA_BITPOS 17
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_SM4_ECB_BITPOS 18
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_SM4_CBC_BITPOS 19
#define ICP_QAT_FW_CAP_CRYPTO_CIPHER_SM4_CTR_BITPOS 20

#define ICP_QAT_FW_CAP_CRYPTO_HASH_MD5_BITPOS 0
#define ICP_QAT_FW_CAP_CRYPTO_HASH_SHA1_BITPOS 1
#define ICP_QAT_FW_CAP_CRYPTO_HASH_SHA224_BITPOS 2
#define ICP_QAT_FW_CAP_CRYPTO_HASH_SHA256_BITPOS 3
#define ICP_QAT_FW_CAP_CRYPTO_HASH_SHA384_BITPOS 4
#define ICP_QAT_FW_CAP_CRYPTO_HASH_SHA512_BITPOS 5
#define ICP_QAT_FW_CAP_CRYPTO_HASH_AES_XCBC_BITPOS 6
#define ICP_QAT_FW_CAP_CRYPTO_HASH_AES_CCM_BITPOS 7
#define ICP_QAT_FW_CAP_CRYPTO_HASH_AES_GCM_BITPOS 8
#define ICP_QAT_FW_CAP_CRYPTO_HASH_KASUMI_F9_BITPOS 9
#define ICP_QAT_FW_CAP_CRYPTO_HASH_SNOW3G_UIA2_BITPOS 10
#define ICP_QAT_FW_CAP_CRYPTO_HASH_AES_CMAC_BITPOS 11
#define ICP_QAT_FW_CAP_CRYPTO_HASH_AES_GMAC_BITPOS 12
#define ICP_QAT_FW_CAP_CRYPTO_HASH_AES_CBCMAC_BITPOS 13
#define ICP_QAT_FW_CAP_CRYPTO_HASH_ZUC_EIA3_BITPOS 14
#define ICP_QAT_FW_CAP_CRYPTO_HASH_SHA3_256_BITPOS 15
#define ICP_QAT_FW_CAP_CRYPTO_HASH_SHA3_224_BITPOS 16
#define ICP_QAT_FW_CAP_CRYPTO_HASH_SHA3_384_BITPOS 17
#define ICP_QAT_FW_CAP_CRYPTO_HASH_SHA3_512_BITPOS 18
#define ICP_QAT_FW_CAP_CRYPTO_HASH_SM3_BITPOS 19

#define ICP_QAT_FW_CAP_CRYPTO_KEYGEN_MGF1_BITPOS 0
#define ICP_QAT_FW_CAP_CRYPTO_KEYGEN_SSL_PRF_BITPOS 1
#define ICP_QAT_FW_CAP_CRYPTO_KEYGEN_TLS10_PRF_BITPOS 2
#define ICP_QAT_FW_CAP_CRYPTO_KEYGEN_TLS12_PRF_BITPOS 3

#define ICP_QAT_FW_CAP_CRYPTO_OTHER_STATEFUL_BITPOS 0

#define ICP_QAT_FW_CAP_CRYPTO_PUBLIC_KEY_RSA_BITPOS 0
#define ICP_QAT_FW_CAP_CRYPTO_PUBLIC_KEY_DH_BITPOS 1
#define ICP_QAT_FW_CAP_CRYPTO_PUBLIC_KEY_DSA_BITPOS 2
#define ICP_QAT_FW_CAP_CRYPTO_PUBLIC_KEY_LN_BITPOS 3
#define ICP_QAT_FW_CAP_CRYPTO_PUBLIC_KEY_ECDH_BITPOS 4
#define ICP_QAT_FW_CAP_CRYPTO_PUBLIC_KEY_ECDSA_BITPOS 5
#define ICP_QAT_FW_CAP_CRYPTO_PUBLIC_KEY_EC_BITPOS 6
#define ICP_QAT_FW_CAP_CRYPTO_PUBLIC_KEY_ECSM2_BITPOS 7

#define ICP_QAT_FW_CAP_CRYPTO_PRIME_GDC_BITPOS 0
#define ICP_QAT_FW_CAP_CRYPTO_PRIME_FERMAT_BITPOS 1
#define ICP_QAT_FW_CAP_CRYPTO_PRIME_MILLERRABIN_BITPOS 2
#define ICP_QAT_FW_CAP_CRYPTO_PRIME_LUCAS_BITPOS 3

/**
 *****************************************************************************
 * @ingroup icp_qat_fw_init_admin
 *
 * @description
 *      ARAM map structure, declaring the allocation of device memory across
 *      services.
 *
 * @usage
 *      A pointer to this structure may be sent in the init_cfg_ptr field of
 *      the INIT_ME type message, with init_cfg_sz set appropriately.
 *
 *****************************************************************************/
struct icp_qat_fw_init_admin_aram_map
{
    /** Inline Egress mask. "1" = AE is working with egress traffic */
    u32 inline_direction_egress_mask;

    /** Inline congestion managmenet profiles set in config file */
    u32 inline_congest_mngt_profile;

    /** Initialise CY AE mask, "1" = AE is used for CY operations */
    u32 cy_ae_mask;

    /** Initialise DC AE mask, "1" = AE is used for DC operations */
    u32 dc_ae_mask;

    /** Number of long words used to define the ARAM regions */
    u32 num_aram_lw_entries;

    /** ARAM region definitions */
    u32 mmp_region_size;
    u32 mmp_region_offset;
    u32 kpt_region_size;
    u32 kpt_region_offset;

    /** Defines size and offset of compression intermediate buffers stored
     * in ARAM (device's on-chip memory).
     */
    u32 inter_buff_aram_region_size;
    u32 inter_buff_aram_region_offset;

    u32 sadb_region_size;
    u32 sadb_region_offset;

    /** IMR (Isolated Memory Region) region definitions
     *
     * IMR is only available on certain systems with QAT 1.8 devices. The
     * offset field of the compression intermediate buffers is relative to
     * the IMR base address. The QAT device driver will determine whether
     * to use ARAM, IMR, or both memory regions for intermediate buffers
     * during device initialisation.
     */
    u32 inter_buff_imr_region_size;
    u32 inter_buff_imr_region_offset;
};

/**
 *****************************************************************************
 * @ingroup icp_qat_fw_init_admin
 * @description
 *      Interpretation for CnV statistics
 *
 *****************************************************************************/

/** Categories of failure which can cause a CnV error to be reported. */
enum icp_qat_fw_cnv_error_type
{
    CNV_ERR_TYPE_NO_ERROR = 0,
    CNV_ERR_TYPE_CHECKSUM_ERROR,
    CNV_ERR_TYPE_DECOMP_PRODUCED_LENGTH_ERROR,
    CNV_ERR_TYPE_DECOMPRESSION_ERROR,
    CNV_ERR_TYPE_TRANSLATION_ERROR,
    CNV_ERR_TYPE_DECOMP_CONSUMED_LENGTH_ERROR,
    CNV_ERR_TYPE_UNKNOWN_ERROR
};

/** Extract category of CnV error, as enumerated above. */
#define CNV_ERROR_TYPE_GET(latest_error)                                       \
    ({                                                                         \
        u16 _lerror_type = (latest_error) >> 12;                               \
        _lerror_type > CNV_ERR_TYPE_UNKNOWN_ERROR                              \
            ? CNV_ERR_TYPE_UNKNOWN_ERROR                                       \
            : (enum icp_qat_fw_cnv_error_type)(_lerror_type);                  \
    })

/** Valid only when the error type is CNV_ERR_TYPE_DECOMP_PRODUCED_LENGTH_ERROR
 * or CNV_ERR_TYPE_DECOMP_CONSUMED_LENGTH_ERROR. Used to determine the amount
 * by which the decompression output length (or consumed length) differed from
 * the expected length. Positive values imply additional data was
 * decompressed/consumed. Negative values imply missing data / unconsumed data.
 * Value saturates at maximum / minimum 12-bit value.
 */
#define CNV_ERROR_LENGTH_DELTA_GET(latest_error)                               \
    ({                                                                         \
        u16 _lerror = (latest_error);                                          \
        (s16)((_lerror & 0x0FFF) | (_lerror & 0x0800 ? 0xF000 : 0));           \
    })

/** Valid only when the error type is CNV_ERR_TYPE_TRANSLATION_ERROR or
 * CNV_ERR_TYPE_DECOMPRESSION_ERROR. Used to identify the cause of the
 * translation / decompression failure. This is a DEFLATE error interpreted in
 * same way as cmp_err_code.
 */
#define CNV_ERROR_DECOMP_STATUS_GET(latest_error) ((s8)((latest_error)&0xFF))

#endif /* _ICP_QAT_FW_INIT_ADMIN_H_ */
