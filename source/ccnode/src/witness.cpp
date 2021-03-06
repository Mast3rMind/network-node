/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * witness.cpp
*/

#include "CCdef.h"
#include "witness.hpp"
#include "block.hpp"
#include "blockchain.hpp"
#include "processblock.hpp"
#include "dbconn.hpp"
#include "util.h"

#include <CCobjects.hpp>
#include <CCcrypto.hpp>
#include <transaction.h>
#include <ed25519/ed25519.h>

#define TRACE_WITNESS	(g_params.trace_witness)

#define WITNESS_TIME_SPACING			(block_time_ms*(CCTICKS_PER_SEC/1000))
#define MIN_BLOCK_WORK_TIME				(block_min_work_ms*(CCTICKS_PER_SEC/1000))
#define WITNESS_NO_WORK_TIME_SPACING	(block_max_time*CCTICKS_PER_SEC)

#define TEST_RANDOM_WITNESS_TIME		(test_block_random_ms*(CCTICKS_PER_SEC/1000))

//#define TEST_RANDOM_WITNESS_ORDER		1
//#define TEST_BUILD_ON_RANDOM			1
//#define TEST_DELAY_LAST_INDELIBLE		1

//#define TEST_MAL_IGNORE_SIG_ORDER		1	// when used, a few bad blocks will be relayed before this node has a fatal blockchain error

//#define TEST_CUZZ						1
//#define TEST_PROCESS_Q				1

//#define TEST_WITNESS_LOSS				16

#define TEST_MIN_MAL_RATIO				(GENESIS_NWITNESSES * GENESIS_MAXMAL)

#define TEST_WITNESS_LOSS_NWITNESSES_LO	((GENESIS_NWITNESSES - GENESIS_MAXMAL) / 2 + GENESIS_MAXMAL + 1)
#define TEST_WITNESS_LOSS_NWITNESSES_HI	GENESIS_NWITNESSES
//#define TEST_WITNESS_LOSS_NWITNESSES_HI	(TEST_WITNESS_LOSS_NWITNESSES_LO + 1)

#ifndef TEST_RANDOM_WITNESS_TIME
#define TEST_RANDOM_WITNESS_TIME		0	// don't test
#endif

#ifndef TEST_RANDOM_WITNESS_ORDER
#define TEST_RANDOM_WITNESS_ORDER		0	// don't test
#endif

#ifndef TEST_BUILD_ON_RANDOM
#define TEST_BUILD_ON_RANDOM			0	// don't test
#endif
#ifndef TEST_DELAY_LAST_INDELIBLE
#define TEST_DELAY_LAST_INDELIBLE		0	// don't test
#endif

#ifndef TEST_MAL_IGNORE_SIG_ORDER
#define TEST_MAL_IGNORE_SIG_ORDER		0	// don't test
#endif

#ifndef TEST_CUZZ
#define TEST_CUZZ						0	// don't test
#endif

#ifndef TEST_PROCESS_Q
#define TEST_PROCESS_Q					0	// don't test
#endif

#ifndef TEST_WITNESS_LOSS
#define TEST_WITNESS_LOSS				0	// don't test
#endif

#ifndef TEST_WITNESS_LOSS
#define TEST_WITNESS_LOSS				0	// don't test
#endif


Witness g_witness;

Witness::Witness()
 :	m_pthread(NULL),
	m_score_genstamp(0),
	m_waiting_on_block(false),
	m_waiting_on_tx(false),
	witness_index(-1),
	block_time_ms(10000),
	block_min_work_ms(1000),
	block_max_time(20),
	test_block_random_ms(-1),
	test_mal(0)
{
	memset(&m_highest_witnessed_level, 0, sizeof(m_highest_witnessed_level));
}

void Witness::Init()
{
	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::Init witness " << witness_index << " test mal " << test_mal;

	if (IsWitness())
	{
		// create static buffer to use when building blocks
		m_blockbuf = SmartBuf(CC_BLOCK_MAX_SIZE);
		CCASSERT(m_blockbuf);

		m_dbconn = new DbConn;
		CCASSERT(m_dbconn);

		if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::Init launching ThreadProc";

		m_pthread = new thread(&Witness::ThreadProc, this);
		CCASSERT(m_pthread);
	}
}

void Witness::DeInit()
{
	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::DeInit";

	ShutdownWork();

	if (m_pthread)
	{
		m_pthread->join();

		delete m_pthread;

		m_pthread = NULL;
	}

	if (m_dbconn)
	{
		delete m_dbconn;

		m_dbconn = NULL;
	}
}

void Witness::ThreadProc()
{
	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::ThreadProc start m_dbconn " << (uintptr_t)m_dbconn;

	SmartBuf last_indelible_block;

	while (!last_indelible_block)	// wait for last_indelible_block to be set
	{
		sleep(1);

		if (g_shutdown)
			return;

		last_indelible_block = g_blockchain.GetLastIndelibleBlock();
	}

	for (unsigned i = 0; i < MAX_NWITNESSES; ++i)
	{
		m_last_indelible_blocks[i] = last_indelible_block;
		m_highest_witnessed_level[i] = g_blockchain.GetLastIndelibleLevel();	// setting to last_indelible_level helps prevent protocol violations; for this to work, make sure blockchain is sync'ed past the witness's last created block before enabling the witness
	}

	int malcount = 0;
	int nonmalcount = 0;
	int failcount = 0;

	while (IsWitness())
	{
		if (TEST_CUZZ) usleep(rand() & (1024*1024-1));

		if (g_blockchain.HasFatalError())
		{
			BOOST_LOG_TRIVIAL(fatal) << "Witness::ThreadProc exiting due to fatal error in blockchain";

			break;
		}

		if (g_shutdown)
			break;

		auto last_indelible_block = g_blockchain.GetLastIndelibleBlock();
		auto block = (Block*)last_indelible_block.data();
		auto wire = block->WireData();
		auto auxp = block->AuxPtr();

		int sim_nwitnesses = auxp->blockchain_params.nwitnesses;

		// the following code block probably doesn't work anymore...
		if (TEST_SIM_ALL_WITNESSES)
		{
			if (TEST_WITNESS_LOSS && sim_nwitnesses > TEST_WITNESS_LOSS_NWITNESSES_LO && (wire->level & TEST_WITNESS_LOSS) == 0)
			{
				usleep(200*1000);	// wait for pending blocks to become indelible

				if (wire->witness < TEST_WITNESS_LOSS_NWITNESSES_LO)	// make the test hard by disabling the witness immediately after it generates a indelible block
				{
					sim_nwitnesses = TEST_WITNESS_LOSS_NWITNESSES_LO;

					BOOST_LOG_TRIVIAL(info) << "Witness::ThreadProc BuildNewBlock simulating witness loss; sim_nwitnesses now = " << sim_nwitnesses;
				}
			}
			else if (TEST_WITNESS_LOSS && sim_nwitnesses < TEST_WITNESS_LOSS_NWITNESSES_HI && (wire->level & TEST_WITNESS_LOSS) > 0)
			{
				sim_nwitnesses = TEST_WITNESS_LOSS_NWITNESSES_HI;

				BOOST_LOG_TRIVIAL(info) << "Witness::ThreadProc BuildNewBlock simulating witness loss fixed; sim_nwitnesses now = " << sim_nwitnesses;
			}

			while (true)
			{
				if (TEST_RANDOM_WITNESS_ORDER)
					witness_index = rand() % sim_nwitnesses;
				else
					witness_index = (witness_index + 1) % sim_nwitnesses;

				if (!test_mal || !GENESIS_MAXMAL || IsMalTest() || nonmalcount * TEST_MIN_MAL_RATIO < malcount || failcount > sim_nwitnesses)
					break;
			}

			BOOST_LOG_TRIVIAL(info) << "Witness::ThreadProc BuildNewBlock simulating witness " << witness_index << " maltest " << IsMalTest();
		}

		//if (witness_index >= 1 && witness_index <= 1)
		//	continue;	// for testing

		auto failed = AttemptNewBlock();

		if (failed)
			failcount++;
		else
			failcount = 0;

		if (IsMalTest())
			malcount++;
		else
			nonmalcount++;

		if (failcount > 200)
		{
			const char *msg = "ERROR Witness::ThreadProc witness appears stuck";

			BOOST_LOG_TRIVIAL(error) << msg;

			//g_blockchain.SetFatalError(msg);	// for testing
		}

		//usleep(250*1000);	// @@@
		//ccsleep(1);		// @@@
	}

	//witness_index = -1;	// no longer acting as a witness

	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::ThreadProc end m_dbconn " << (uintptr_t)m_dbconn;
}

uint32_t Witness::NextTurnTicks()
{
	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::NextTurnTicks";

	// stub

	return 0;
}

bool Witness::IsMalTest()
{
	if (!IsWitness() || !test_mal)
		return false;

	if (TEST_SIM_ALL_WITNESSES && witness_index > 0)
		return false;

	return true;
}

uint64_t Witness::FindBestOwnScore(SmartBuf last_indelible_block)
{
	uint64_t bestscore = 0;
	SmartBuf smartobj;

	if (m_test_ignore_order)
		m_dbconn->ProcessQClearValidObjs(PROCESS_Q_TYPE_BLOCK);

	for (unsigned offset = 0; ; ++offset)
	{
		m_dbconn->ProcessQGetNextValidObj(PROCESS_Q_TYPE_BLOCK, offset, &smartobj);

		if (!smartobj)
			break;

		auto block = (Block*)smartobj.data();
		auto wire = block->WireData();
		auto auxp = block->AuxPtr();

		if (wire->witness != witness_index)
			continue;

		auto score = block->CalcSkipScore(-1, last_indelible_block, m_score_genstamp, m_test_ignore_order);
		if (score > bestscore)
			bestscore = score;

		if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestOwnScore witness " << (unsigned)wire->witness << " maltest " << m_test_ignore_order << " score " << hex << score << " bestscore " << bestscore << dec << " level " << wire->level << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

		if (m_test_ignore_order)
			m_dbconn->ProcessQUpdateValidObj(PROCESS_Q_TYPE_BLOCK, auxp->oid, PROCESS_Q_STATUS_VALID, score);	// for maltest, store score in sqlite; later we'll use this to ensure maltest doesn't generate many blocks with the same score
	}

	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestOwnScore witness " << witness_index << " maltest " << m_test_ignore_order << " returning " << hex << bestscore << dec;

	return bestscore;
}

SmartBuf Witness::FindBestBuildingBlock(SmartBuf last_indelible_block, uint64_t m_highest_witnessed_level, uint64_t bestscore)
{
	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestBuildingBlock witness " << witness_index << " maltest " << m_test_ignore_order << " highest witnessed level " << m_highest_witnessed_level << " bestscore " << hex << bestscore << dec;

	SmartBuf smartobj, bestobj;

	if (m_test_ignore_order || TEST_BUILD_ON_RANDOM)
		m_dbconn->ProcessQRandomizeValidObjs(PROCESS_Q_TYPE_BLOCK);

	for (unsigned offset = 0; ; ++offset)
	{
		m_dbconn->ProcessQGetNextValidObj(PROCESS_Q_TYPE_BLOCK, offset, &smartobj);

		if (!smartobj)
			break;

		auto block = (Block*)smartobj.data();
		auto wire = block->WireData();
		auto auxp = block->AuxPtr();

		if (m_highest_witnessed_level > wire->level && !m_test_ignore_order)
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestBuildingBlock witness " << witness_index << " highest witnessed level " << m_highest_witnessed_level << " > building block level " << wire->level << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

			continue;
		}

		if (block->CheckBadSigOrder(witness_index))
		{
			if (m_test_ignore_order)
			{
				BOOST_LOG_TRIVIAL(info) << "Witness::FindBestBuildingBlock witness " << witness_index << " ignoring bad sig order for building block level " << wire->level << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));
			}
			else
			{
				if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestBuildingBlock witness " << witness_index << " bad sig order building block level " << wire->level << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

				continue;
			}
		}

		auto score = block->CalcSkipScore(witness_index, last_indelible_block, m_score_genstamp, m_test_ignore_order);

		if (bestscore >= score && !m_test_ignore_order)
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestBuildingBlock witness " << witness_index << " bestscore " << hex << bestscore << " >= score " << score << dec << " for building block level " << wire->level << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

			continue;
		}

		if (m_test_ignore_order)
		{
			auto count = m_dbconn->ProcessQCountValidObjs(PROCESS_Q_TYPE_BLOCK, score);

			if (count >= auxp->blockchain_params.maxmal + 1)
			{
				if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestBuildingBlock witness " << witness_index << " maltest true count " << count << " for score " << hex << score << dec;

				continue;
			}
		}

		bestobj = smartobj;
		bestscore = score;

		if (m_test_ignore_order)
			break;		// take the first block

		if (score && TEST_BUILD_ON_RANDOM)
			break;		// take the first legal block
	}

	if (bestobj && TRACE_WITNESS)
	{
		auto block = (Block*)bestobj.data();
		auto wire = block->WireData();
		auto auxp = block->AuxPtr();

		if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestBuildingBlock witness " << witness_index << " returning best score " << hex << bestscore << dec << " best block level " << wire->level << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));
	}
	else if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FindBestBuildingBlock witness " << witness_index << " found no building block";

	return bestobj;
}

bool Witness::AttemptNewBlock()
{
	SmartBuf priorobj, bestblock;

	uint32_t min_time = 0;
	uint32_t max_time = 0;

	while (true)
	{
		if (g_shutdown)
			return true;

		uint64_t last_indelible_level = 0;

		if (!bestblock || HaveNewBlockWork())
		{
			ResetNewBlockWork();

			SmartBuf last_indelible_block;

			if (TEST_DELAY_LAST_INDELIBLE)
				last_indelible_block = m_last_indelible_blocks[TEST_SIM_ALL_WITNESSES * witness_index];

			if (!last_indelible_block)
				last_indelible_block = g_blockchain.GetLastIndelibleBlock();

			auto block = (Block*)last_indelible_block.data();
			auto wire = block->WireData();
			auto auxp = block->AuxPtr();

			last_indelible_level = wire->level;

			static bool last_maltest = false;

			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::AttemptNewBlock looking for best prior block witness " << witness_index << " maltest " << m_test_ignore_order << " last indelible block witness " << (unsigned)wire->witness << " level " << wire->level << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

			if (last_indelible_block != m_last_last_indelible_block || m_test_ignore_order != last_maltest)
			{
				m_last_last_indelible_block = last_indelible_block;
				last_maltest = m_test_ignore_order;

				if (++m_score_genstamp == 0)
					++m_score_genstamp;
			}

			auto bestscore = FindBestOwnScore(last_indelible_block);

			bestblock = FindBestBuildingBlock(last_indelible_block, m_highest_witnessed_level[TEST_SIM_ALL_WITNESSES * witness_index], bestscore);
		}

		if (!bestblock)
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(debug) << "Witness::AttemptNewBlock no building block found for witness " << witness_index << " maltest " << m_test_ignore_order;

			if (TEST_DELAY_LAST_INDELIBLE)
				m_last_indelible_blocks[TEST_SIM_ALL_WITNESSES * witness_index] = g_blockchain.GetLastIndelibleBlock();

			WaitForWork(true, false, 0);

			continue;	// retry with new blocks
		}

		//cerr << "priorobj " << priorobj.BasePtr() << " bestblock " << bestblock.BasePtr() << " priorobj != bestblock " << (priorobj != bestblock) << endl;

		if (priorobj != bestblock)
		{
			priorobj = bestblock;

			auto block = (Block*)priorobj.data();
			auto wire = block->WireData();
			auto auxp = block->AuxPtr();
			auto skip = Block::ComputeSkip(wire->witness, witness_index, auxp->blockchain_params.nwitnesses);

			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::AttemptNewBlock best prior block witness " << witness_index << " maltest " << m_test_ignore_order << " best block witness " << (unsigned)wire->witness << " skip " << skip << " level " << wire->level << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

			StartNewBlock();

			auto now = ccticks();
			min_time = now;
			if (ccticks_elapsed(now, auxp->announce_time) <= 0)
			{
				min_time = auxp->announce_time;

				if (TEST_RANDOM_WITNESS_TIME < 0)
					min_time += (skip + 1) * WITNESS_TIME_SPACING;
				else if (TEST_RANDOM_WITNESS_TIME > 0)
					min_time += rand() % (TEST_RANDOM_WITNESS_TIME * 2);	// double the input value so it is an average time

				if (ccticks_elapsed(now, min_time) > 60*60*CCTICKS_PER_SEC)
					min_time = now;

				//cerr << "announce " << auxp->announce_time << " skip " << skip << " min_time " << min_time << endl;
			}

			if (ccticks_elapsed(m_block_start_time, min_time) <= MIN_BLOCK_WORK_TIME && TEST_RANDOM_WITNESS_TIME < 0)
				min_time = m_block_start_time + MIN_BLOCK_WORK_TIME;

			max_time = min_time;
			auto delibletxs = g_blockchain.ChainHasDelibleTxs(priorobj, last_indelible_level);
			if (!delibletxs && TEST_RANDOM_WITNESS_TIME < 0)
				max_time += WITNESS_NO_WORK_TIME_SPACING - WITNESS_TIME_SPACING;

			if (!min_time)
				min_time = 1;	// because zero means no wait

			if (!max_time)
				max_time = 1;	// because zero means no wait

			//cerr << "announce " << auxp->announce_time << " now " << now << " skip " << skip << " min_time " << min_time << " max_time " << max_time << endl;
		}

		CCASSERT(priorobj);
		CCASSERT(min_time);
		CCASSERT(max_time);

		auto rc = BuildNewBlock(min_time, max_time, priorobj);

		if (rc == BUILD_NEWBLOCK_STATUS_ERROR)
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(debug) << "Witness::AttemptNewBlock BuildNewBlock could not create a new block; maltest " << m_test_ignore_order;

			return true;
		}

		if (!WaitForWork(true, true, (m_newblock_bufpos ? min_time : max_time)))
			break;

		if (HaveNewBlockWork())
			continue;	// check to restart with a different prior block

		if (ccticks_elapsed(ccticks(), (m_newblock_bufpos ? min_time : max_time)) <= 0)
			break;
	}

	if (g_shutdown)
		return true;

	auto smartobj = FinishNewBlock(priorobj);

	if (!smartobj)
	{
		if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(debug) << "Witness::AttemptNewBlock FinishNewBlock could not create a new block by witness " << witness_index << " maltest " << m_test_ignore_order;

		return true;
	}

	auto block = (Block*)smartobj.data();
	auto wire = block->WireData();
	auto auxp = block->AuxPtr();

	if (m_test_ignore_order)
		m_test_ignore_order = block->CheckBadSigOrder(-1);

	if (!m_test_is_double_spend && !m_test_ignore_order)
		m_highest_witnessed_level[TEST_SIM_ALL_WITNESSES * witness_index] = wire->level;

	if (1)
	{
		lock_guard<FastSpinLock> lock(g_cout_lock);
		cerr << "  created block level " << wire->level << " witness " << (unsigned)wire->witness << " skip " << auxp->skip << " size " << (block->ObjSize() < 1000 ? " " : "") << block->ObjSize() << " oid " << buf2hex(&auxp->oid, 3, 0) << ".. prior " << buf2hex(&wire->prior_oid, 3, 0) << "..";
		if (m_test_is_double_spend)
			cerr << " double-spend";
		if (m_test_ignore_order)
			cerr << " bad-order";
		cerr << endl;
	}

	relay_request_wire_params_t req_params;
	memset(&req_params, 0, sizeof(req_params));
	memcpy(&req_params.oid, &auxp->oid, sizeof(ccoid_t));

	m_dbconn->RelayObjsInsert(0, CC_TAG_BLOCK, req_params, RELAY_STATUS_DOWNLOADED, 0);	// so we don't download it after sending it to someone else

	if (TEST_PROCESS_Q)
	{
		// test blocks going through the Process_Q
		m_dbconn->ProcessQEnqueueValidate(PROCESS_Q_TYPE_BLOCK, smartobj, &wire->prior_oid, wire->level, PROCESS_Q_STATUS_PENDING, 0, 0, 0);
		// the new block needs to be placed into the blockchain before this witness attempts to create another block
		// since we're not normally using this code path, we'll do this the easy way by just sleeping a little and hoping Process_Q is finished by then
		usleep(200*1000);
	}
	else
	{
		bool isvalid = true;

		if (m_test_is_double_spend || m_test_ignore_order)
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(debug) << "Witness::AttemptNewBlock witness " << witness_index << " block with double-spend " << m_test_is_double_spend << " bad sig order " << m_test_ignore_order << " won't be considered when building additional blocks, level " << wire->level << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t));

			isvalid = false;
		}

		g_processblock.ValidObjsBlockInsert(m_dbconn, smartobj, m_txbuf, isvalid, isvalid);
	}

	return false;
}

void Witness::StartNewBlock()
{
	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::StartNewBlock";

	m_block_start_time = ccticks();
	m_newblock_bufpos = 0;
	m_newblock_next_tx_seqnum = 1;

	m_test_ignore_order = false;
	m_test_try_persistent_double_spend = false;
	m_test_try_inter_double_spend = false;
	m_test_try_intra_double_spend = false;
	m_test_is_double_spend = false;

	if (IsMalTest() && (GENESIS_MAXMAL > 0 || TEST_MAL_IGNORE_SIG_ORDER))
	{
		if (!(rand() & 7))
			m_test_ignore_order = true;				// add param for this?
	}

	if (IsMalTest())
	{
		if (!(rand() & 7))
			m_test_try_persistent_double_spend = true;
		else if (!(rand() & 7))
			m_test_try_inter_double_spend = true;
		else if (!(rand() & 3))
			m_test_try_intra_double_spend = true;
	}

	m_dbconn->TempSerialnumClear((void*)TEMP_SERIALS_WITNESS_BLOCKP);
}

Witness::BuildNewBlockStatus Witness::BuildNewBlock(uint32_t& min_time, uint32_t max_time, SmartBuf priorobj)
{
	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::BuildNewBlock min_time " << min_time << " max_time " << max_time;

	BuildNewBlockStatus build_status = BUILD_NEWBLOCK_STATUS_OK;

	auto output = m_blockbuf.data();
	CCASSERT(output);

	static const uint32_t bufsize = TEST_SMALL_BUFS ? 2*1024 : (CC_BLOCK_MAX_SIZE - sizeof(CCObject::Header) - sizeof(BlockWireHeader));
	CCASSERT(m_blockbuf.size() >= bufsize);

	static const int TXARRAYSIZE = TEST_SMALL_BUFS ? 5 : 100;
	static array<SmartBuf, TXARRAYSIZE> txarray;	// not thread safe

	bool test_no_delete_persistent_txs = IsMalTest();

	while (!g_shutdown)
	{
		SetNewTxWork(false);

		int64_t next_block_seqnum = 0;		// this value tells ValidObjsFindNew to fill a SmartBuf array

		auto ntx = m_dbconn->ValidObjsFindNew(next_block_seqnum, m_newblock_next_tx_seqnum, (uint8_t*)&txarray, TXARRAYSIZE);

		if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::BuildNewBlock witness " << witness_index << " fetched " << ntx << " potential tx's";

		if (ntx >= TXARRAYSIZE)
			SetNewTxWork(true);		// still more there

		for (unsigned i = 0; i < ntx; ++i)
		{
			auto smartobj = txarray[i];
			txarray[i].ClearRef();

			auto bufp = smartobj.BasePtr();
			auto obj = (CCObject*)smartobj.data();
			auto tag = obj->ObjTag();

			if (tag != CC_TAG_TX_WIRE)
			{
				BOOST_LOG_TRIVIAL(error) << "Witness::BuildNewBlock obj tag " << tag << " bufp " << (uintptr_t)bufp;

				continue;
			}

			auto txwire = obj->ObjPtr();
			auto txsize = obj->ObjSize();
			auto txbody = obj->BodyPtr();
			auto bodysize = obj->BodySize();
			uint32_t newsize = bodysize + 2 * sizeof(uint32_t);				// need space for tag and size

			if (m_newblock_bufpos + newsize > bufsize)
			{
				build_status = BUILD_NEWBLOCK_STATUS_FULL;

				if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::BuildNewBlock witness " << witness_index << " skipping tx bufp " << (uintptr_t)bufp << " because it doesn't fit ";

				continue;	// keep looking for a smaller tx
			}

			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::BuildNewBlock witness " << witness_index << " checking tx bufp " << (uintptr_t)bufp << " size " << txsize;

			auto rc = tx_from_wire(m_txbuf, (char*)txwire, txsize);
			if (rc)
				continue;

			g_blockchain.CheckCreatePseudoSerialnum(m_txbuf, txwire, txsize);

			bool badserial = 0;

			for (unsigned i = 0; i < m_txbuf.nin; ++i)
			{
				auto rc = g_blockchain.CheckSerialnum(m_dbconn, priorobj, TEMP_SERIALS_WITNESS_BLOCKP, (test_no_delete_persistent_txs ? SmartBuf() : smartobj), &m_txbuf.input[i].S_serialnum, sizeof(m_txbuf.input[i].S_serialnum));
				if (rc)
				{
					badserial = rc;

					if (rc == 4 && m_test_try_persistent_double_spend)
					{
						m_test_is_double_spend = true;
						m_test_try_persistent_double_spend = false;
					}

					if (rc == 3 && m_test_try_inter_double_spend)
					{
						m_test_is_double_spend = true;
						m_test_try_inter_double_spend = false;
					}

					if (rc == 2 && m_test_try_intra_double_spend)
					{
						m_test_is_double_spend = true;
						m_test_try_intra_double_spend = false;
					}

					break;
				}
			}

			if (badserial && !m_test_is_double_spend)
			{
				if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::BuildNewBlock witness " << witness_index << " skipping tx bufp " << (uintptr_t)bufp << " with bad serialnum status " << badserial;

				continue;
			}

			int badinsert = 0;

			for (unsigned i = 0; i < m_txbuf.nin; ++i)
			{
				// if the witness accepts tx's with duplicate serialnums (which it does for maltest),
				// we can end up with extra serialnum's in the tempdb that were put there before the duplicate was detected and the tx rejected
				// that can result in the later rejection of a valid block from another witness that contains the same serialnum so it appears to be a double-spend
				// but the non-mal witnesses won't have this problem, and they will accept the valid block

				auto rc = m_dbconn->TempSerialnumInsert(&m_txbuf.input[i].S_serialnum, sizeof(m_txbuf.input[i].S_serialnum), (void*)TEMP_SERIALS_WITNESS_BLOCKP);
				if (rc)
				{
					badinsert = rc;

					if (m_test_try_intra_double_spend)
					{
						m_test_is_double_spend = true;
						m_test_try_intra_double_spend = false;
					}

					break;
				}
			}

			if (badinsert && !m_test_is_double_spend)
			{
				if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::BuildNewBlock witness " << witness_index << " skipping tx bufp " << (uintptr_t)bufp << " due to TempSerialnumInsert failure " << badinsert;

				continue;
			}

			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::BuildNewBlock witness " << witness_index << " adding tx bufp " << (uintptr_t)bufp << " size " << newsize;

			//cerr << "ntx " << ntx << " adding txarray[" << i << "] bufp " << (uintptr_t)bufp << " obj " << (uintptr_t)obj << " body " << (uintptr_t)txbody << " size = " << newsize << endl;
			//cerr << "ntx " << ntx << " adding txarray[" << i << "] bufp " << (uintptr_t)bufp << " size " << newsize << endl;

			if (!m_newblock_bufpos)
			{
				auto now = ccticks();
				if (ccticks_elapsed(now, min_time) <= MIN_BLOCK_WORK_TIME && TEST_RANDOM_WITNESS_TIME < 0)
				{
					min_time = now + MIN_BLOCK_WORK_TIME;
					if (ccticks_elapsed(min_time, max_time) <= 0)
						min_time = max_time;
					if (!min_time)
						min_time = 1;	// because zero means no wait
				}
			}

			const uint32_t newtag = CC_TAG_TX_BLOCK;
			copy_to_buf(&newsize, sizeof(newsize), m_newblock_bufpos, output, bufsize);
			copy_to_buf(&newtag, sizeof(newtag), m_newblock_bufpos, output, bufsize);
			copy_to_buf(txbody, bodysize, m_newblock_bufpos, output, bufsize);
		}

		if (HaveNewBlockWork())
			break;

		if (!HaveNewTxWork())
			break;

		if (ccticks_elapsed(ccticks(), (m_newblock_bufpos ? min_time : max_time)) <= 0)
			break;
	}

	if (m_newblock_bufpos > bufsize)
	{
		BOOST_LOG_TRIVIAL(error) << "Witness::BuildNewBlock witness " << witness_index << " buffer overflow m_newblock_bufpos " << m_newblock_bufpos << " bufsize " << bufsize;

		return BUILD_NEWBLOCK_STATUS_ERROR;
	}

	return build_status;
}

SmartBuf Witness::FinishNewBlock(SmartBuf priorobj)
{
	SmartBuf smartobj;

	auto priorblock = (Block*)priorobj.data();
	CCASSERT(priorblock);

	auto priorwire = priorblock->WireData();
	auto priorauxp = priorblock->AuxPtr();

	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FinishNewBlock witness " << witness_index << " prior block level " << priorwire->level << " oid " << buf2hex(&priorauxp->oid, sizeof(ccoid_t));

	CCASSERT(witness_index >= 0);

	if (witness_index >= priorauxp->blockchain_params.nwitnesses)
	{
		BOOST_LOG_TRIVIAL(info) << "Witness::FinishNewBlock skipping witness " << witness_index << " because it exceeds block prior block nwitnesses " << priorauxp->blockchain_params.nwitnesses;

		return SmartBuf();
	}

	// copy block to SmartBuf

	auto objsize = m_newblock_bufpos + sizeof(CCObject::Header) + sizeof(BlockWireHeader);

	smartobj = SmartBuf(objsize + sizeof(CCObject::Preamble));
	if (!smartobj)
	{
		BOOST_LOG_TRIVIAL(error) << "Witness::FinishNewBlock witness " << witness_index << " smartobj failed";

		return SmartBuf();
	}

	auto block = (Block*)smartobj.data();
	CCASSERT(block);

	block->SetTag(CC_TAG_BLOCK);
	block->SetSize(objsize);

	auto wire = block->WireData();

	memcpy(&wire->prior_oid, &priorauxp->oid, sizeof(ccoid_t));
	wire->timestamp = _time64(NULL);
	if (wire->timestamp < priorwire->timestamp)
		wire->timestamp = priorwire->timestamp;
	wire->level = priorwire->level + 1;
	wire->witness = witness_index;

	if (m_newblock_bufpos)
	{
		//if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FinishNewBlock adding m_txbuf's at txdata " << (uintptr_t)block->TxData() << " size " << m_newblock_bufpos << " data " << buf2hex(output, 16);
		if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::FinishNewBlock adding tx's size " << m_newblock_bufpos << " to block level " << wire->level << " bufp " << (uintptr_t)smartobj.BasePtr() << " prior bufp " << (uintptr_t)priorobj.BasePtr() << " prior oid " << buf2hex(&priorauxp->oid, sizeof(ccoid_t));

		memcpy(block->TxData(), m_blockbuf.data(), m_newblock_bufpos);
	}

	auto auxp = block->SetupAuxBuf(smartobj);
	if (!auxp)
	{
		BOOST_LOG_TRIVIAL(error) << "Witness::FinishNewBlock witness " << witness_index << " SetupAuxBuf failed";

		return SmartBuf();
	}

	block->ChainToPriorBlock(priorobj);

#if ROTATE_BLOCK_SIGNING_KEYS
	int keynum = 0;
	if (TEST_SIM_ALL_WITNESSES)
		keynum = witness_index;

	CCRandom(&auxp->witness_params.next_signing_private_key[keynum], sizeof(auxp->witness_params.next_signing_private_key[keynum]));
	ed25519_publickey(&auxp->witness_params.next_signing_private_key[keynum][0], &auxp->blockchain_params.signing_keys[witness_index][0]);
	memcpy(&wire->witness_next_signing_public_key, &auxp->blockchain_params.signing_keys[witness_index], sizeof(wire->witness_next_signing_public_key));
#endif

	block_hash_t block_hash;
	block->CalcHash(block_hash);
	auxp->SetHash(block_hash);

	block->SignOrVerify(false);

	if (block->SignOrVerify(true))
	{
		BOOST_LOG_TRIVIAL(error) << "Witness::FinishNewBlock witness " << witness_index << " verify own signature failed";

		// !!! there's currently no way to restart and witness because there is no way to reset it's signing key after the genesis block
		// that means the only way to currently to start a witness is to restart with a new blockchain
		// for now, alert the user of the problem, and then stop acting as a witness

		const char *msg = "Verify own signature failed --> this server will stop acting as a witness.";

		BOOST_LOG_TRIVIAL(error) << "Witness::FinishNewBlock " << msg;

		{
			lock_guard<FastSpinLock> lock(g_cout_lock);
			cerr << "ERROR: " << msg << endl;
		}

		witness_index = -1;	// stop acting as a witness, so we don't get this message again

		return SmartBuf();
	}

	ccoid_t oid;
	block->CalcOid(block_hash, oid);
	auxp->SetOid(oid);

	if (m_dbconn->TempSerialnumUpdate((void*)TEMP_SERIALS_WITNESS_BLOCKP, smartobj.BasePtr(), wire->level))
	{
		BOOST_LOG_TRIVIAL(error) << "Witness::FinishNewBlock witness " << witness_index << " TempSerialnumUpdate failed";

		return SmartBuf();
	}

	BOOST_LOG_TRIVIAL(info) << "Witness::FinishNewBlock built new block level " << wire->level << " witness " << (unsigned)wire->witness << " size " << block->ObjSize() << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t)) << " prior oid " << buf2hex(&wire->prior_oid, sizeof(ccoid_t));

	if (m_test_is_double_spend)
		BOOST_LOG_TRIVIAL(info) << "Witness::FinishNewBlock built double-spend test block level " << wire->level << " witness " << (unsigned)wire->witness << " size " << block->ObjSize() << " oid " << buf2hex(&auxp->oid, sizeof(ccoid_t)) << " prior oid " << buf2hex(&wire->prior_oid, sizeof(ccoid_t));

	return smartobj;
}

void Witness::NotifyNewWork(bool is_block)
{
	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::NotifyNewWork is_block " << is_block << " waiting_on_block " << m_waiting_on_block << " waiting_on_tx " << m_waiting_on_tx;

	lock_guard<mutex> lock(m_work_mutex);

	if (is_block)
	{
		m_have_new_block = true;

		if (!m_waiting_on_block)
			return;
	}
	else
	{
		m_have_new_tx = true;

		if (!m_waiting_on_tx)
			return;
	}

	m_work_condition_variable.notify_one();
}

void Witness::ShutdownWork()
{
	lock_guard<mutex> lock(m_work_mutex);

	m_work_condition_variable.notify_all();
}

int Witness::WaitForWork(bool bwait4block, bool bwait4tx, uint32_t target_time)
{
	if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::WaitForWork for block " << bwait4block << " for tx " << bwait4tx << " target_time " << target_time;

	unique_lock<mutex> lock(m_work_mutex, defer_lock);

	bool needs_lock = true;

	while (!g_shutdown)
	{
		if (bwait4block && HaveNewBlockWork())
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::WaitForWork HaveNewBlockWork true";

			return 1;
		}

		if (bwait4tx && HaveNewTxWork())
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::WaitForWork HaveNewTxWork true GetNextTxSeqnum() " << DbConnValidObjs::GetNextTxSeqnum() << " m_newblock_next_tx_seqnum " << m_newblock_next_tx_seqnum;

			return 1;
		}

		if (needs_lock)
		{
			lock.lock();

			needs_lock = false;

			continue;		// recheck with lock
		}

		int elapse = -1;

		if (target_time)
		{
			auto now = ccticks();
			elapse = ccticks_elapsed(now, target_time);
			if (elapse <= 1)
			{
				if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::WaitForWork target time " << target_time << " reached at " << now;

				return 0;
			}
		}

		m_waiting_on_block = bwait4block;
		m_waiting_on_tx = bwait4tx;

		#if CCTICKS_PER_SEC != 1000
		#error fix wait: units != milliseconds
		#endif

		if (target_time)
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::WaitForWork waiting for " << elapse << " milliseconds";

			m_work_condition_variable.wait_for(lock, chrono::milliseconds(elapse));
		}
		else
		{
			if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::WaitForWork waiting";

			m_work_condition_variable.wait(lock);
		}

		m_waiting_on_block = false;
		m_waiting_on_tx = false;

		if (TRACE_WITNESS) BOOST_LOG_TRIVIAL(trace) << "Witness::WaitForWork rechecking conditions at " << ccticks();
	}

	return 0;
}