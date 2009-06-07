/***************************************************************************
 *   Copyright (C) 2007-2009 by Lothar May                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <net/servergame.h>
#include <net/servergamestate.h>
#include <net/serverlobbythread.h>
#include <net/serverexception.h>
#include <net/senderhelper.h>
#include <net/receiverhelper.h>
#include <net/socket_msg.h>
#include <core/loghelper.h>
#include <game.h>
#include <localenginefactory.h>
#include <tools.h>

#include <boost/bind.hpp>


#define SERVER_CHECK_VOTE_KICK_INTERVAL_MSEC	500
#define SERVER_KICK_TIMEOUT_ADD_DELAY_SEC		2

using namespace std;


ServerGame::ServerGame(ServerLobbyThread &lobbyThread, u_int32_t id, const string &name, const string &pwd, const GameData &gameData, unsigned adminPlayerId, GuiInterface &gui, ConfigFile *playerConfig)
: m_adminPlayerId(adminPlayerId), m_lobbyThread(lobbyThread), m_gui(gui),
  m_gameData(gameData), m_curState(NULL), m_id(id), m_name(name), m_password(pwd), m_playerConfig(playerConfig),
  m_gameNum(1), m_curPetitionId(1), m_stateTimerId(0)
{
	LOG_VERBOSE("Game object " << GetId() << " created.");

	m_receiver.reset(new ReceiverHelper);

	m_voteKickTimerId = GetLobbyThread().GetTimerManager().RegisterTimer(
		SERVER_CHECK_VOTE_KICK_INTERVAL_MSEC,
		boost::bind(&ServerGame::TimerVoteKick, this),
		true);

	SetState(SERVER_INITIAL_STATE::Instance());
}

ServerGame::~ServerGame()
{
	GetLobbyThread().GetTimerManager().UnregisterTimer(m_voteKickTimerId);
	GetLobbyThread().GetTimerManager().UnregisterTimer(m_stateTimerId);

	LOG_VERBOSE("Game object " << GetId() << " destructed.");
}

u_int32_t
ServerGame::GetId() const
{
	return m_id;
}

const std::string &
ServerGame::GetName() const
{
	return m_name;
}

void
ServerGame::AddSession(SessionWrapper session)
{
	if (session.sessionData)
		GetState().HandleNewSession(*this, session);
}

void
ServerGame::RemovePlayer(unsigned playerId, unsigned errorCode)
{
	SessionWrapper tmpSession = GetSessionManager().GetSessionByUniquePlayerId(playerId);
	// Only kick if the player was found.
	if (tmpSession.sessionData.get())
		SessionError(tmpSession, errorCode);
}

void
ServerGame::HandlePacket(SessionWrapper session, boost::shared_ptr<NetPacket> packet)
{
	if (session.sessionData && packet)
		GetState().ProcessPacket(*this, session, packet);
}

GameState
ServerGame::GetCurRound() const
{
	return static_cast<GameState>(GetGame().getCurrentHand()->getCurrentRound());
}

void
ServerGame::SendToAllPlayers(boost::shared_ptr<NetPacket> packet, SessionData::State state)
{
	GetSessionManager().SendToAllSessions(GetLobbyThread().GetSender(), packet, state);
}

void
ServerGame::RemoveAllSessions()
{
	// Called from lobby thread.
	// Clean up ALL sessions which are left.
	GetSessionManager().ForEach(boost::bind(&ServerLobbyThread::RemoveSessionFromGame, boost::ref(m_lobbyThread), _1));
}

void
ServerGame::TimerVoteKick()
{
	// Check whether someone should be kicked, or whether a vote kick should be aborted.
	// Only one vote kick can be active at a time.
	if (m_voteKickData)
	{
		// Prepare some values.
		const PlayerIdList playerIds(GetPlayerIdList());
		int votesRequiredToKick = m_voteKickData->numVotesToKick - m_voteKickData->numVotesInFavourOfKicking;
		int playersAllowedToVote = 0;
		// We need to count the number of players which are still allowed to vote.
		PlayerIdList::const_iterator player_i = playerIds.begin();
		PlayerIdList::const_iterator player_end = playerIds.end();
		while (player_i != player_end)
		{
			if (find(m_voteKickData->votedPlayerIds.begin(), m_voteKickData->votedPlayerIds.end(), *player_i) == m_voteKickData->votedPlayerIds.end())
				playersAllowedToVote++;
			++player_i;
		}
		bool abortPetition = false;
		bool doKick = false;
		EndPetitionReason reason;

		// 1. Enough votes to kick the player.
		if (m_voteKickData->numVotesInFavourOfKicking >= m_voteKickData->numVotesToKick)
		{
			reason = PETITION_END_ENOUGH_VOTES;
			abortPetition = true;
			doKick = true;
		}
		// 2. Several players left the game, so a kick is no longer possible.
		else if (votesRequiredToKick > playersAllowedToVote)
		{
			reason = PETITION_END_NOT_ENOUGH_PLAYERS;
			abortPetition = true;
		}
		// 3. The kick has become invalid because the player to be kicked left.
		else if (!IsValidPlayer(m_voteKickData->kickPlayerId))
		{
			reason = PETITION_END_PLAYER_LEFT;
			abortPetition = true;
		}
		// 4. A kick request timed out (because not everyone voted).
		else if (m_voteKickData->voteTimer.elapsed().total_seconds() >= m_voteKickData->timeLimitSec)
		{
			reason = PETITION_END_TIMEOUT;
			abortPetition = true;
		}
		if (abortPetition)
		{
			boost::shared_ptr<NetPacket> endPetition(new NetPacketEndKickPlayerPetition);
			NetPacketEndKickPlayerPetition::Data endPetitionData;
			endPetitionData.petitionId = m_voteKickData->petitionId;
			endPetitionData.numVotesAgainstKicking = m_voteKickData->numVotesAgainstKicking;
			endPetitionData.numVotesInFavourOfKicking = m_voteKickData->numVotesInFavourOfKicking;
			endPetitionData.playerKicked = doKick;
			endPetitionData.endReason = reason;

			static_cast<NetPacketEndKickPlayerPetition *>(endPetition.get())->SetData(endPetitionData);
			SendToAllPlayers(endPetition, SessionData::Game);

			// Perform kick.
			if (doKick)
				InternalKickPlayer(m_voteKickData->kickPlayerId);
			// This petition has ended.
			m_voteKickData.reset();
		}
	}
}

void
ServerGame::InternalStartGame()
{
	// Set order of players.
	AssignPlayerNumbers();

	// Initialize the game.
	GuiInterface &gui = GetGui();
	PlayerDataList playerData = GetFullPlayerDataList();

	// Create EngineFactory
	boost::shared_ptr<EngineFactory> factory(new LocalEngineFactory(m_playerConfig)); // LocalEngine erstellen

	// Set start data.
	StartData startData;
	startData.numberOfPlayers = playerData.size();

	int tmpDealerPos = 0;
	Tools::getRandNumber(0, startData.numberOfPlayers-1, 1, &tmpDealerPos, 0);
	// The Player Id is not continuous. Therefore, the start dealer position
	// needs to be converted to a player Id, and cannot be directly generated
	// as player Id.
	PlayerDataList::const_iterator player_i = playerData.begin();
	PlayerDataList::const_iterator player_end = playerData.end();

	int tmpPos = 0;
	while (player_i != player_end)
	{
		startData.startDealerPlayerId = static_cast<unsigned>((*player_i)->GetUniqueId());
		if (tmpPos == tmpDealerPos)
			break;
		++tmpPos;
		++player_i;
	}
	if (player_i == player_end)
		throw ServerException(__FILE__, __LINE__, ERR_NET_DEALER_NOT_FOUND, 0);

	SetStartData(startData);

	m_game.reset(new Game(&gui, factory, playerData, GetGameData(), GetStartData(), GetNextGameNum()));

	GetLobbyThread().NotifyStartingGame(GetId());
}

void
ServerGame::ResetGame()
{
	m_game.reset();
}

void
ServerGame::InternalKickPlayer(unsigned playerId)
{
	SessionWrapper tmpSession = GetSessionManager().GetSessionByUniquePlayerId(playerId);
	// Only kick if the player was found.
	if (tmpSession.sessionData.get())
		MoveSessionToLobby(tmpSession, NTF_NET_REMOVED_KICKED);
	// KICKING COMPUTER PLAYERS IS BUGGY AND OCCASIONALLY CAUSES A CRASH
	// Disabled for now.
	//else
	//{
	//	boost::shared_ptr<PlayerData> tmpData(RemoveComputerPlayer(playerId));
	//	if (tmpData)
	//		RemovePlayerData(tmpData, NTF_NET_REMOVED_KICKED);
	//}
}

void
ServerGame::InternalAskVoteKick(SessionWrapper byWhom, unsigned playerIdWho, unsigned timeoutSec)
{
	if (IsRunning() && byWhom.playerData)
	{
		// Retrieve only the number of human players.
		size_t numPlayers = GetSessionManager().GetPlayerIdList().size();
		if (numPlayers > 2)
		{
			// Check whether the player to be kicked exists.
			if (IsValidPlayer(playerIdWho))
			{
				// Lock the vote kick data.
				if (!m_voteKickData)
				{
					// Initiate a vote kick.
					unsigned playerIdByWhom = byWhom.playerData->GetUniqueId();
					m_voteKickData.reset(new VoteKickData);
					m_voteKickData->petitionId = m_curPetitionId++;
					m_voteKickData->kickPlayerId = playerIdWho;
					m_voteKickData->numVotesToKick = static_cast<int>(ceil(numPlayers / 3. * 2.));
					m_voteKickData->timeLimitSec = timeoutSec + SERVER_KICK_TIMEOUT_ADD_DELAY_SEC;
					// Consider first vote.
					m_voteKickData->numVotesInFavourOfKicking = 1;
					m_voteKickData->votedPlayerIds.push_back(playerIdByWhom);

					boost::shared_ptr<NetPacket> startPetition(new NetPacketStartKickPlayerPetition);
					NetPacketStartKickPlayerPetition::Data startPetitionData;
					startPetitionData.petitionId = m_voteKickData->petitionId;
					startPetitionData.proposingPlayerId = playerIdByWhom;
					startPetitionData.kickPlayerId = m_voteKickData->kickPlayerId;
					startPetitionData.kickTimeoutSec = timeoutSec;
					startPetitionData.numVotesNeededToKick = m_voteKickData->numVotesToKick;

					static_cast<NetPacketStartKickPlayerPetition *>(startPetition.get())->SetData(startPetitionData);
					SendToAllPlayers(startPetition, SessionData::Game);
				}
				else
					InternalDenyAskVoteKick(byWhom, playerIdWho, KICK_DENIED_OTHER_IN_PROGRESS);
			}
			else
				InternalDenyAskVoteKick(byWhom, playerIdWho, KICK_DENIED_INVALID_PLAYER_ID);
		}
		else
			InternalDenyAskVoteKick(byWhom, playerIdWho, KICK_DENIED_TOO_FEW_PLAYERS);
	}
	else
		InternalDenyAskVoteKick(byWhom, playerIdWho, KICK_DENIED_INVALID_STATE);
}

void
ServerGame::InternalDenyAskVoteKick(SessionWrapper byWhom, unsigned playerIdWho, DenyKickPlayerReason reason)
{
	boost::shared_ptr<NetPacket> denyPetition(new NetPacketAskKickPlayerDenied);
	NetPacketAskKickPlayerDenied::Data denyPetitionData;
	denyPetitionData.playerId = playerIdWho;
	denyPetitionData.denyReason = reason;
	static_cast<NetPacketAskKickPlayerDenied *>(denyPetition.get())->SetData(denyPetitionData);
	GetLobbyThread().GetSender().Send(byWhom.sessionData, denyPetition);
}

void
ServerGame::InternalVoteKick(SessionWrapper byWhom, unsigned petitionId, KickVote vote)
{
	if (IsRunning() && byWhom.playerData)
	{
		// Check whether this is the valid petition id.
		if (m_voteKickData && m_voteKickData->petitionId == petitionId)
		{
			// Check whether the player already voted.
			unsigned playerId = byWhom.playerData->GetUniqueId();
			if (find(m_voteKickData->votedPlayerIds.begin(), m_voteKickData->votedPlayerIds.end(), playerId) == m_voteKickData->votedPlayerIds.end())
			{
				m_voteKickData->votedPlayerIds.push_back(playerId);
				if (vote == KICK_VOTE_IN_FAVOUR)
					m_voteKickData->numVotesInFavourOfKicking++;
				else
					m_voteKickData->numVotesAgainstKicking++;
				// Send update notification.
				boost::shared_ptr<NetPacket> updatePetition(new NetPacketKickPlayerPetitionUpdate);
				NetPacketKickPlayerPetitionUpdate::Data updatePetitionData;
				updatePetitionData.petitionId = m_voteKickData->petitionId;
				updatePetitionData.numVotesAgainstKicking = m_voteKickData->numVotesAgainstKicking;
				updatePetitionData.numVotesInFavourOfKicking = m_voteKickData->numVotesInFavourOfKicking;
				updatePetitionData.numVotesNeededToKick = m_voteKickData->numVotesToKick;

				static_cast<NetPacketKickPlayerPetitionUpdate *>(updatePetition.get())->SetData(updatePetitionData);
				SendToAllPlayers(updatePetition, SessionData::Game);
			}
			else
				InternalDenyVoteKick(byWhom, petitionId, VOTE_DENIED_ALREADY_VOTED);
		}
		else
			InternalDenyVoteKick(byWhom, petitionId, VOTE_DENIED_INVALID_PETITION);
	}
	else
		InternalDenyVoteKick(byWhom, petitionId, VOTE_DENIED_IMPOSSIBLE);
}

void
ServerGame::InternalDenyVoteKick(SessionWrapper byWhom, unsigned petitionId, DenyVoteReason reason)
{
	boost::shared_ptr<NetPacket> denyVote(new NetPacketVoteKickPlayerDenied);
	NetPacketVoteKickPlayerDenied::Data denyVoteData;
	denyVoteData.petitionId = petitionId;
	denyVoteData.denyReason = reason;
	static_cast<NetPacketVoteKickPlayerDenied *>(denyVote.get())->SetData(denyVoteData);
	GetLobbyThread().GetSender().Send(byWhom.sessionData, denyVote);
}

PlayerDataList
ServerGame::GetFullPlayerDataList() const
{
	PlayerDataList playerList(GetSessionManager().GetPlayerDataList());
	boost::mutex::scoped_lock lock(m_computerPlayerListMutex);
	copy(m_computerPlayerList.begin(), m_computerPlayerList.end(), back_inserter(playerList));

	return playerList;
}

boost::shared_ptr<PlayerData>
ServerGame::GetPlayerDataByUniqueId(unsigned playerId) const
{
	boost::shared_ptr<PlayerData> tmpPlayer;
	SessionWrapper session = GetSessionManager().GetSessionByUniquePlayerId(playerId);
	if (session.playerData.get())
	{
		tmpPlayer = session.playerData;
	}
	else
	{
		boost::mutex::scoped_lock lock(m_computerPlayerListMutex);
		PlayerDataList::const_iterator i = m_computerPlayerList.begin();
		PlayerDataList::const_iterator end = m_computerPlayerList.end();
		while (i != end)
		{
			if ((*i)->GetUniqueId() == playerId)
			{
				tmpPlayer = *i;
				break;
			}
			++i;
		}
	}
	return tmpPlayer;
}

PlayerIdList
ServerGame::GetPlayerIdList() const
{
	PlayerIdList idList(GetSessionManager().GetPlayerIdList());
	boost::mutex::scoped_lock lock(m_computerPlayerListMutex);
	PlayerDataList::const_iterator i = m_computerPlayerList.begin();
	PlayerDataList::const_iterator end = m_computerPlayerList.end();
	while (i != end)
	{
		idList.push_back((*i)->GetUniqueId());
		++i;
	}

	return idList;
}

bool
ServerGame::IsPlayerConnected(const std::string &name) const
{
	return GetSessionManager().IsPlayerConnected(name);
}

bool
ServerGame::IsRunning() const
{
	return m_game.get() != NULL;
}

unsigned
ServerGame::GetAdminPlayerId() const
{
	return m_adminPlayerId;
}

void
ServerGame::SetAdminPlayerId(unsigned playerId)
{
	m_adminPlayerId = playerId;
}

void
ServerGame::AddComputerPlayer(boost::shared_ptr<PlayerData> player)
{
	{
		boost::mutex::scoped_lock lock(m_computerPlayerListMutex);
		m_computerPlayerList.push_back(player);
	}
	GetLobbyThread().AddComputerPlayer(player);
}

boost::shared_ptr<PlayerData>
ServerGame::RemoveComputerPlayer(unsigned playerId)
{
	boost::shared_ptr<PlayerData> tmpPlayer;
	{
		boost::mutex::scoped_lock lock(m_computerPlayerListMutex);
		PlayerDataList::iterator i = m_computerPlayerList.begin();
		PlayerDataList::iterator end = m_computerPlayerList.end();
		while (i != end)
		{
			if ((*i)->GetUniqueId() == playerId)
			{
				tmpPlayer = *i;
				m_computerPlayerList.erase(i);
				break;
			}
			++i;
		}
	}
	GetLobbyThread().RemoveComputerPlayer(tmpPlayer);
	return tmpPlayer;
}

bool
ServerGame::IsComputerPlayerActive(unsigned playerId) const
{
	bool retVal = false;
	boost::mutex::scoped_lock lock(m_computerPlayerListMutex);
	PlayerDataList::const_iterator i = m_computerPlayerList.begin();
	PlayerDataList::const_iterator end = m_computerPlayerList.end();
	while (i != end)
	{
		if ((*i)->GetUniqueId() == playerId)
			retVal = true;
		++i;
	}
	return retVal;
}

void
ServerGame::ResetComputerPlayerList()
{
	boost::mutex::scoped_lock lock(m_computerPlayerListMutex);

	PlayerDataList::iterator i = m_computerPlayerList.begin();
	PlayerDataList::iterator end = m_computerPlayerList.end();

	while (i != end)
	{
		GetLobbyThread().RemoveComputerPlayer(*i);
		RemovePlayerData(*i, NTF_NET_REMOVED_ON_REQUEST);
		++i;
	}

	m_computerPlayerList.clear();
}

void
ServerGame::GracefulRemoveSession(SessionWrapper session, int reason)
{
	if (!session.sessionData.get())
		throw ServerException(__FILE__, __LINE__, ERR_NET_INVALID_SESSION, 0);
	GetSessionManager().RemoveSession(session.sessionData->GetId());

	boost::shared_ptr<PlayerData> tmpPlayerData = session.playerData;
	if (tmpPlayerData.get() && !tmpPlayerData->GetName().empty())
	{
		RemovePlayerData(tmpPlayerData, reason);
	}
}

void
ServerGame::RemovePlayerData(boost::shared_ptr<PlayerData> player, int reason)
{
	if (player->GetRights() == PLAYER_RIGHTS_ADMIN)
	{
		// Find new admin for the game
		PlayerDataList playerList(GetSessionManager().GetPlayerDataList());
		if (!playerList.empty())
		{
			boost::shared_ptr<PlayerData> newAdmin = playerList.front();
			SetAdminPlayerId(newAdmin->GetUniqueId());
			newAdmin->SetRights(PLAYER_RIGHTS_ADMIN);
			// Notify game state on admin change
			GetState().NotifyGameAdminChanged(*this);
			// Send "Game Admin Changed" to clients.
			boost::shared_ptr<NetPacket> adminChanged(new NetPacketGameAdminChanged);
			NetPacketGameAdminChanged::Data adminChangedData;
			adminChangedData.playerId = newAdmin->GetUniqueId(); // Choose next player as admin.
			static_cast<NetPacketGameAdminChanged *>(adminChanged.get())->SetData(adminChangedData);
			GetSessionManager().SendToAllSessions(GetLobbyThread().GetSender(), adminChanged, SessionData::Game);

			GetLobbyThread().NotifyGameAdminChanged(GetId(), newAdmin->GetUniqueId());
		}
	}
	// Reset player rights.
	player->SetRights(PLAYER_RIGHTS_NORMAL);

	// Send "Player Left" to clients.
	boost::shared_ptr<NetPacket> thisPlayerLeft(new NetPacketPlayerLeft);
	NetPacketPlayerLeft::Data thisPlayerLeftData;
	thisPlayerLeftData.playerId = player->GetUniqueId();
	thisPlayerLeftData.removeReason = reason;
	static_cast<NetPacketPlayerLeft *>(thisPlayerLeft.get())->SetData(thisPlayerLeftData);
	GetSessionManager().SendToAllSessions(GetLobbyThread().GetSender(), thisPlayerLeft, SessionData::Game);

	GetLobbyThread().NotifyPlayerLeftGame(GetId(), player->GetUniqueId());
}

void
ServerGame::ErrorRemoveSession(SessionWrapper session)
{
	GetLobbyThread().RemoveSessionFromGame(session);
	GracefulRemoveSession(session, NTF_NET_INTERNAL);
}

void
ServerGame::SessionError(SessionWrapper session, int errorCode)
{
	if (!session.sessionData.get())
		throw ServerException(__FILE__, __LINE__, ERR_NET_INVALID_SESSION, 0);
	ErrorRemoveSession(session);
	GetLobbyThread().SessionError(session, errorCode);
}

void
ServerGame::MoveSessionToLobby(SessionWrapper session, int reason)
{
	GracefulRemoveSession(session, reason);
	// Reset ready flag - just in case it is set, player may leave at any time.
	session.sessionData->ResetReadyFlag();
	GetLobbyThread().ReAddSession(session, reason);
}

void
ServerGame::RemoveDisconnectedPlayers()
{
	// This should only be called between hands.
	if (m_game.get())
	{
		PlayerListIterator i = m_game->getSeatsList()->begin();
		PlayerListIterator end = m_game->getSeatsList()->end();
		while (i != end)
		{
			boost::shared_ptr<PlayerInterface> tmpPlayer = *i;
			if ((tmpPlayer->getMyType() == PLAYER_TYPE_HUMAN && !GetSessionManager().IsPlayerConnected(tmpPlayer->getMyUniqueID()))
				|| (tmpPlayer->getMyType() == PLAYER_TYPE_COMPUTER && !IsComputerPlayerActive(tmpPlayer->getMyUniqueID())))
			{
				// Setting player cash to 0 will deactivate the player.
				tmpPlayer->setMyCash(0);
				tmpPlayer->setNetSessionData(boost::shared_ptr<SessionData>());
			}
			++i;
		}
	}
}

size_t
ServerGame::GetCurNumberOfPlayers() const
{
	return GetFullPlayerDataList().size();
}

void
ServerGame::AssignPlayerNumbers()
{
	int playerNumber = 0;

	PlayerDataList playerList = GetFullPlayerDataList();
	PlayerDataList::iterator player_i = playerList.begin();
	PlayerDataList::iterator player_end = playerList.end();

	while (player_i != player_end)
	{
		(*player_i)->SetNumber(playerNumber);
		++playerNumber;
		++player_i;
	}
}

bool
ServerGame::IsValidPlayer(unsigned playerId) const
{
	bool retVal = false;
	const PlayerIdList list(GetPlayerIdList());
	if (find(list.begin(), list.end(), playerId) != list.end())
		retVal = true;
	return retVal;
}

SessionManager &
ServerGame::GetSessionManager()
{
	return m_sessionManager;
}

const SessionManager &
ServerGame::GetSessionManager() const
{
	return m_sessionManager;
}

ServerLobbyThread &
ServerGame::GetLobbyThread()
{
	return m_lobbyThread;
}

ServerCallback &
ServerGame::GetCallback()
{
	return m_gui;
}

ServerGameState &
ServerGame::GetState()
{
	assert(m_curState);
	return *m_curState;
}

void
ServerGame::SetState(ServerGameState &newState)
{
	if (m_curState)
		m_curState->Exit(*this);
	m_curState = &newState;
	m_curState->Enter(*this);
}

unsigned
ServerGame::GetStateTimerId() const
{
	return m_stateTimerId;
}

void
ServerGame::SetStateTimerId(unsigned newTimerId)
{
	m_stateTimerId = newTimerId;
}

ReceiverHelper &
ServerGame::GetReceiver()
{
	assert(m_receiver.get());
	return *m_receiver;
}

Game &
ServerGame::GetGame()
{
	assert(m_game.get());
	return *m_game;
}

const Game &
ServerGame::GetGame() const
{
	assert(m_game.get());
	return *m_game;
}

const GameData &
ServerGame::GetGameData() const
{
	return m_gameData;
}

const StartData &
ServerGame::GetStartData() const
{
	return m_startData;
}

void
ServerGame::SetStartData(const StartData &startData)
{
	m_startData = startData;
}

bool
ServerGame::IsPasswordProtected() const
{
	return !m_password.empty();
}

bool
ServerGame::CheckPassword(const string &password) const
{
	return (password == m_password);
}

GuiInterface &
ServerGame::GetGui()
{
	return m_gui;
}

unsigned
ServerGame::GetNextGameNum()
{
	return m_gameNum++;
}
