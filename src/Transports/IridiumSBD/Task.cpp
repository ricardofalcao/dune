//***************************************************************************
// Copyright 2007-2013 Universidade do Porto - Faculdade de Engenharia      *
// Laboratório de Sistemas e Tecnologia Subaquática (LSTS)                  *
//***************************************************************************
// This file is part of DUNE: Unified Navigation Environment.               *
//                                                                          *
// Commercial Licence Usage                                                 *
// Licencees holding valid commercial DUNE licences may use this file in    *
// accordance with the commercial licence agreement provided with the       *
// Software or, alternatively, in accordance with the terms contained in a  *
// written agreement between you and Universidade do Porto. For licensing   *
// terms, conditions, and further information contact lsts@fe.up.pt.        *
//                                                                          *
// European Union Public Licence - EUPL v.1.1 Usage                         *
// Alternatively, this file may be used under the terms of the EUPL,        *
// Version 1.1 only (the "Licence"), appearing in the file LICENCE.md       *
// included in the packaging of this file. You may not use this work        *
// except in compliance with the Licence. Unless required by applicable     *
// law or agreed to in writing, software distributed under the Licence is   *
// distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF     *
// ANY KIND, either express or implied. See the Licence for the specific    *
// language governing permissions and limitations at                        *
// https://www.lsts.pt/dune/licence.                                        *
//***************************************************************************
// Author: Ricardo Martins                                                  *
//***************************************************************************

// ISO C++ 98 headers.
#include <queue>

// DUNE headers.
#include <DUNE/DUNE.hpp>

// Local headers.
#include "Driver.hpp"
#include "TxRequest.hpp"

namespace Transports
{
  namespace IridiumSBD
  {
    using DUNE_NAMESPACES;

    //! %Task arguments.
    struct Arguments
    {
      //! Serial port device.
      std::string uart_dev;
      //! Serial port baud rate.
      unsigned uart_baud;
      //! Mailbox check periodicity.
      double mbox_check_per;
      //! Maximum transmission rate.
      unsigned max_tx_rate;
    };

    struct Task: public DUNE::Tasks::Task
    {
      //! Serial port handle.
      SerialPort* m_uart;
      //! Driver handler.
      Driver* m_driver;
      //! List of transmission requests.
      std::list<TxRequest*> m_tx_requests;
      //! Number of MT messages queued at the GSS.
      unsigned m_queued_mt;
      //! Mailbox check timer.
      Counter<double> m_mbox_check_timer;
      //! Task arguments.
      Arguments m_args;
      //! Active transmission request.
      TxRequest* m_tx_request;

      //! Constructor.
      //! @param[in] name task name.
      //! @param[in] ctx context.
      Task(const std::string& name, Tasks::Context& ctx):
        DUNE::Tasks::Task(name, ctx),
        m_uart(NULL),
        m_driver(NULL),
        m_tx_request(NULL)
      {
        param("Serial Port - Device", m_args.uart_dev)
        .defaultValue("")
        .description("Serial port device used to communicate with the modem");

        param("Serial Port - Baud Rate", m_args.uart_baud)
        .defaultValue("19200")
        .description("Serial port baud rate");

        param("Mailbox Check - Periodicity", m_args.mbox_check_per)
        .units(Units::Second)
        .defaultValue("300")
        .description("Amount of time without alert rings or "
                     "MT SBDs before doing a mailbox check");

        param("Maximum Transmission Rate", m_args.max_tx_rate)
        .units(Units::Second)
        .defaultValue("0")
        .description("");

        bind<IMC::IridiumMsgTx>(this);
      }

      //! Destructor.
      ~Task(void)
      {
        Memory::clear(m_tx_request);

        while (!m_tx_requests.empty())
        {
          TxRequest* req = m_tx_requests.front();
          m_tx_requests.pop_front();
          sendTxRequestStatus(req, IMC::IridiumTxStatus::TXSTATUS_ERROR,
                              DTR("task is shutting down"));
          delete req;
        }
      }

      //! Update internal state with new parameter values.
      void
      onUpdateParameters(void)
      {
        m_mbox_check_timer.setTop(m_args.mbox_check_per);
        if (m_driver != NULL)
          m_driver->setTxRateMax(m_args.max_tx_rate);
      }

      //! Reserve entity identifiers.
      void
      onEntityReservation(void)
      {
      }

      //! Resolve entity names.
      void
      onEntityResolution(void)
      {
      }

      //! Acquire resources.
      void
      onResourceAcquisition(void)
      {
      }

      //! Initialize resources.
      void
      onResourceInitialization(void)
      {
        m_uart = new SerialPort(m_args.uart_dev, m_args.uart_baud);
        m_driver = new Driver(this, m_uart);
        m_driver->initialize();
        m_driver->setTxRateMax(m_args.max_tx_rate);
        debug("manufacturer: %s", m_driver->getManufacturer().c_str());
        debug("model: %s", m_driver->getModel().c_str());
        debug("IMEI: %s", m_driver->getIMEI().c_str());
      }

      //! Release resources.
      void
      onResourceRelease(void)
      {
        if (m_driver)
        {
          m_driver->stopAndJoin();
          delete m_driver;
          m_driver = NULL;
        }
      }

      void
      consume(const IMC::IridiumMsgTx* msg)
      {
        // FIXME: check if req_id already exists.
        // FIXME: check MTU.
        debug("queueing message");
        unsigned dst_adr = resolveSystemName(msg->destination);
        unsigned src_adr = msg->getSource();
        unsigned src_eid = msg->getSourceEntity();
        TxRequest* request = new TxRequest(src_adr, src_eid, dst_adr,
                                           msg->req_id, msg->ttl, msg->data);

        enqueueTxRequest(request);
        sendTxRequestStatus(request, IMC::IridiumTxStatus::TXSTATUS_QUEUED);
      }

      void
      sendTxRequestStatus(const TxRequest* request,
                          IMC::IridiumTxStatus::StatusCodeEnum code,
                          const std::string& text = "")
      {
        IMC::IridiumTxStatus status;
        status.setDestination(request->getSource());
        status.setDestinationEntity(request->getSourceEntity());
        status.req_id = request->getId();
        status.status = code;
        status.text = text;
        dispatch(status);
      }

      void
      enqueueTxRequest(TxRequest* request)
      {
        std::list<TxRequest*>::iterator itr = m_tx_requests.begin();
        for ( ; itr != m_tx_requests.end(); ++itr)
        {
          if (request->getTimeToLive() < (*itr)->getTimeToLive())
          {
            m_tx_requests.insert(itr, request);
            return;
          }
        }

        m_tx_requests.insert(m_tx_requests.end(), request);
      }

      void
      dequeueTxRequest(unsigned msn)
      {
        if (m_tx_request == NULL)
          return;

        if (!m_tx_request->hasValidMSN() || (m_tx_request->getMSN() != msn))
          return;

        debug("dequeing message");
        m_driver->clearBufferMO();
        sendTxRequestStatus(m_tx_request, IMC::IridiumTxStatus::TXSTATUS_OK);
        Memory::clear(m_tx_request);
      }

      void
      invalidateTxRequest(unsigned msn, unsigned err_code)
      {
        if (m_tx_request == NULL)
          return;

        if (!m_tx_request->hasValidMSN() || (m_tx_request->getMSN() != msn))
          return;

        debug("invalidating MSN");
        m_tx_request->invalidateMSN();

        sendTxRequestStatus(m_tx_request, IMC::IridiumTxStatus::TXSTATUS_ERROR,
                            String::str(DTR("failed with error %u"), err_code));

        enqueueTxRequest(m_tx_request);
        m_tx_request = NULL;
      }

      void
      handleSBD(void)
      {
        uint8_t bfr[340];
        unsigned rv = m_driver->readBufferMT(bfr, sizeof(bfr));
        if (rv > 2)
        {
          IMC::IridiumMsgRx sbd;
          sbd.setSource((bfr[0] << 8) | bfr[1]);
          sbd.setDestination(getSystemId());
          sbd.data.assign(bfr + 2, bfr + rv);
          sbd.toText(std::cerr);
          dispatch(sbd, DF_KEEP_SRC_EID);
        }
        else
        {
          err("invalid SBD message of size %u", rv);
        }
      }

      void
      handleSessionResult(void)
      {
        const SessionResult& res = m_driver->getSessionResult();

        if (res.isSuccessMO())
        {
          m_mbox_check_timer.reset();
          dequeueTxRequest(res.getSequenceMO());
        }
        else
        {
          invalidateTxRequest(res.getSequenceMO(), res.getStatusMO());
        }

        if (res.getStatusMT() == 1)
          handleSBD();
      }

      void
      processQueue(void)
      {
        if (m_driver->isBusy())
          return;

        if (m_driver->hasSessionResult())
          handleSessionResult();

        if (m_driver->getRSSI() == 0)
          return;

        if (m_driver->isCooling())
          return;

        if (m_tx_requests.empty())
        {
          if (m_driver->hasRingAlert())
            m_driver->checkMailBoxAlert();
          else if ((m_driver->getQueuedMT()) > 0 || m_mbox_check_timer.overflow())
            m_driver->checkMailBox();
        }
        else
        {
          TxRequest* request = m_tx_requests.front();
          m_tx_requests.pop_front();
          unsigned msn = m_driver->getMOMSN();
          request->setMSN(msn);
          m_driver->sendSBD(request->getData());
        }
      }

      //! Main loop.
      void
      onMain(void)
      {
        while (!stopping())
        {
          waitForMessages(1.0);
          processQueue();
        }
      }
    };
  }
}

DUNE_TASK
