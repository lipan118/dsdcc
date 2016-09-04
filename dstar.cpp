///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016 Edouard Griffiths, F4EXB.                                  //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include "dstar.h"
#include "dsd_decoder.h"
#include "descramble.h"

namespace DSDcc
{

const int DSDDstar::dW[72] = {

        // 0-11
        0, 0,
        3, 2,
        1, 1,
        0, 0,
        1, 1,
        0, 0,

        // 12-23
        3, 2,
        1, 1,
        3, 2,
        1, 1,
        0, 0,
        3, 2,

        // 24-35
        0, 0,
        3, 2,
        1, 1,
        0, 0,
        1, 1,
        0, 0,

        // 36-47
        3, 2,
        1, 1,
        3, 2,
        1, 1,
        0, 0,
        3, 2,

        // 48-59
        0, 0,
        3, 2,
        1, 1,
        0, 0,
        1, 1,
        0, 0,

        // 60-71
        3, 2,
        1, 1,
        3, 3,
        2, 1,
        0, 0,
        3, 3,
};

const int DSDDstar::dX[72] = {

        // 0-11
        10, 22,
        11, 9,
        10, 22,
        11, 23,
        8, 20,
        9, 21,

        // 12-23
        10, 8,
        9, 21,
        8, 6,
        7, 19,
        8, 20,
        9, 7,

        // 24-35
        6, 18,
        7, 5,
        6, 18,
        7, 19,
        4, 16,
        5, 17,

        // 36-47
        6, 4,
        5, 17,
        4, 2,
        3, 15,
        4, 16,
        5, 3,

        // 48-59
        2, 14,
        3, 1,
        2, 14,
        3, 15,
        0, 12,
        1, 13,

        // 60-71
        2, 0,
        1, 13,
        0, 12,
        10, 11,
        0, 12,
        1, 13,
};

DSDDstar::DSDDstar(DSDDecoder *dsdDecoder) :
        m_dsdDecoder(dsdDecoder)
{
    reset_header_strings();
}

DSDDstar::~DSDDstar()
{
}

void DSDDstar::reset_header_strings()
{
    m_rpt1.clear();
    m_rpt2.clear();
    m_yourSign.clear();
    m_mySign.clear();
}

void DSDDstar::init(bool header)
{
    //fprintf(stderr, "DSDDstar::init: symbol %d (%d)\n", m_dsdDecoder->m_state.symbolcnt, m_dsdDecoder->m_dsdSymbol.getSymbol());

    if (header)
    {
        framecount = 0;
    }
    else
    {
        framecount = 1; //just saw a sync frame; there should be 20 not 21 till the next
    }

    if ((m_dsdDecoder->m_opts.errorbars == 1) && (!header))
    {
        m_dsdDecoder->getLogger().log( "e:"); // print this only for voice/data frames
    }

    sync_missed = 0;
    bitbuffer = 0;
    m_symbolIndex = 0;
    m_symbolIndexHD = 0;
    m_dsdDecoder->m_voice1On = true;
}

void DSDDstar::initVoiceFrame()
{
    //fprintf(stderr, "DSDDstar::initVoiceFrame\n");
    memset(m_dsdDecoder->ambe_fr, 0, 96);
    memset((void *) m_dsdDecoder->m_mbeDVFrame1, 0, 9); // initialize DVSI frame
    // voice frame
    w = dW;
    x = dX;
}
void DSDDstar::initDataFrame()
{
}

void DSDDstar::process()
{
    if (m_symbolIndex < 72) // voice frame
    {
        if (m_symbolIndex == 0) {
            initVoiceFrame();
        }

        processVoice();
    }
    else if (m_symbolIndex < 97) // data frame
    {
        if (m_symbolIndex == 72) {
            initDataFrame();
        }

        processData();
    }
    else
    {
        m_dsdDecoder->m_voice1On = false;
        m_dsdDecoder->resetFrameSync(); // end
    }

    m_symbolIndex++;
}

void DSDDstar::processHD()
{
    int dibit = m_dsdDecoder->m_dsdSymbol.getDibit(); // get dibit from symbol and store it in HD cache
    radioheaderbuffer[m_symbolIndexHD] = dibit;

    if (m_symbolIndexHD == 660-1)
    {
        dstar_header_decode();
        init(); // init for DSTAR
        m_dsdDecoder->m_fsmState = DSDDecoder::DSDprocessDSTAR; // go to DSTAR
    }
    else
    {
        m_symbolIndexHD++;
    }
}

void DSDDstar::processVoice()
{
    int dibit = m_dsdDecoder->m_dsdSymbol.getDibit(); // get dibit from symbol and store it in cache
    m_dibitCache[m_symbolIndex] = dibit;

    bitbuffer <<= 1;

    if (dibit == 1) {
        bitbuffer |= 0x01;
    }

    if ((bitbuffer & 0x00FFFFFF) == 0x00AAB468)
    {
        // we're slipping bits
        m_dsdDecoder->getLogger().log( "DSDDstar::processVoice: sync in voice after i=%d, restarting\n", m_symbolIndex);
        //ugh just start over
        m_symbolIndex = -1; // restart a frame sequence - incremented at the end of process() hence -1
        w = dW;
        x = dX;
        framecount = 1;
    }
    else
    {
		//fprintf(stderr, "DSDDstar::processVoice: i: %d w: %d x: %d val: %d\n", m_symbolIndex, *w, *x, (1 & dibit));

        m_dsdDecoder->ambe_fr[*w][*x] = (1 & dibit);
		w++;
		x++;

        //m_dsdDecoder->m_mbeDVFrame[m_symbolIndex/8] |= (1 & dibit)<<(m_symbolIndex%8); // store bits in order in DVSI frame LSB first
        storeSymbolDV(m_symbolIndex, (1 & dibit)); // store bits in order in DVSI frame
    }

    if (m_symbolIndex == 72-1)
    {
    	//fprintf(stderr, "DSDDstar::processVoice: MBE: symbol %d (%d)\n", m_dsdDecoder->m_state.symbolcnt, m_dsdDecoder->m_dsdSymbol.getSymbol());
        if (m_dsdDecoder->m_opts.errorbars == 1) {
            m_dsdDecoder->getLogger().log("\nMBE: ");
        }

        m_dsdDecoder->m_mbeDecoder1.processFrame(0, m_dsdDecoder->ambe_fr, 0);
        m_dsdDecoder->m_mbeDVReady1 = true; // Indicate that a DVSI frame is available
    }
}

void DSDDstar::storeSymbolDV(int bitindex, unsigned char bit, bool lsbFirst)
{
    if (lsbFirst)
    {
        m_dsdDecoder->m_mbeDVFrame1[bitindex/8] |= bit << (bitindex%8); // store bits in order in DVSI frame LSB first
    }
    else
    {
        m_dsdDecoder->m_mbeDVFrame1[8 - (bitindex/8)] |= bit << (7 - (bitindex%8)); // store bits in order in DVSI frame MSB first
    }
}


void DSDDstar::processData()
{
    bool terminate = false;
    bool dataEnd = (m_symbolIndex >= 96-1);
    int dibit = m_dsdDecoder->m_dsdSymbol.getDibit(); // get dibit from symbol

    bitbuffer <<= 1;

    if (dibit == 1) {
        bitbuffer |= 0x01;
    }

    if ((bitbuffer & 0x00FFFFFF) == 0x00AAB468)
    {
        // looking if we're slipping bits
        if (m_symbolIndex < 96-1) // Premature end
        {
            m_dsdDecoder->getLogger().log( "DSDDstar::processData: sync after i=%d\n", m_symbolIndex);
            dataEnd = true;
        }
    }

    if (dataEnd) // last dibit in data frame
    {
    	//fprintf(stderr, "DSDDstar::processData: symbol %d (%d)\n", m_dsdDecoder->m_state.symbolcnt, m_dsdDecoder->m_dsdSymbol.getSymbol());

        slowdata[0] = (bitbuffer >> 16) & 0x000000FF;
        slowdata[1] = (bitbuffer >> 8) & 0x000000FF;
        slowdata[2] = (bitbuffer) & 0x000000FF;
        slowdata[3] = 0;

        if ((bitbuffer & 0x00FFFFFF) == 0x00AAB468)
        {
            //We got sync!
            m_dsdDecoder->getLogger().log("DSDDstar::processData: Sync on framecount = %d\n", framecount);
            sync_missed = 0;
        }
        else if ((bitbuffer & 0x00FFFFFF) == 0xAAAAAA)
        {
            //End of transmission
            m_dsdDecoder->getLogger().log("DSDDstar::processData: End of transmission\n");
            terminate = true;
        }
        else if (framecount % 21 == 0)
        {
            m_dsdDecoder->getLogger().log("DSDDstar::processData: Missed sync on framecount = %d, value = %x/%x/%x\n",
                    framecount, slowdata[0], slowdata[1], slowdata[2]);
            sync_missed++;
        }
        else if (framecount != 0 && (bitbuffer & 0x00FFFFFF) != 0x000000)
        {
            slowdata[0] ^= 0x70;
            slowdata[1] ^= 0x4f;
            slowdata[2] ^= 0x93;
            //printf("unscrambled- %s",slowdata);
        }
        else if (framecount == 0)
        {
            //printf("never scrambled-%s\n",slowdata);
        }

        //fprintf(stderr, "DSDDstar::processData: sync_missed: %d terminate: %d\n", sync_missed, (terminate ? 1 : 0));

        if (terminate)
        {
            reset_header_strings();
            m_dsdDecoder->resetFrameSync(); // end
        }
        else if (sync_missed < 3)
        {
            m_symbolIndex = -1; // restart a frame sequence - incremented at the end of process() hence -1
            framecount++;
            //fprintf(stderr, "DSDDstar::processData: restart frame: framecount: %d\n", framecount);
        }
        else
        {
            reset_header_strings();
            m_dsdDecoder->resetFrameSync(); // end
        }
    }
}

void DSDDstar::dstar_header_decode()
{
    int radioheaderbuffer2[660];
    unsigned char radioheader[41];
    int octetcount, bitcount, loop;
    unsigned char bit2octet[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
    unsigned int FCSinheader;
    unsigned int FCScalculated;
    int len;

    Descramble::scramble(radioheaderbuffer, radioheaderbuffer2);
    Descramble::deinterleave(radioheaderbuffer2, radioheaderbuffer);
    len = Descramble::FECdecoder(radioheaderbuffer, radioheaderbuffer2);
    memset(radioheader, 0, 41);

    // note we receive 330 bits, but we only use 328 of them (41 octets)
    // bits 329 and 330 are unused
    octetcount = 0;
    bitcount = 0;

    for (loop = 0; loop < 328; loop++)
    {
        if (radioheaderbuffer2[loop])
        {
            radioheader[octetcount] |= bit2octet[bitcount];
        };

        bitcount++;

        // increase octetcounter and reset bitcounter every 8 bits
        if (bitcount >= 8)
        {
            octetcount++;
            bitcount = 0;
        }
    }

    // print header - TODO: store it in the state object for access from caller
    m_dsdDecoder->getLogger().log("\nDSTAR HEADER: ");
    //printf("FLAG1: %02X - FLAG2: %02X - FLAG3: %02X\n", radioheader[0],
    //      radioheader[1], radioheader[2]);
    m_rpt2 = std::string((const char *) &radioheader[3], 8);
    m_dsdDecoder->getLogger().log("RPT 2: %s ", m_rpt2.c_str());
//    m_dsdDecoder->getLogger().log("RPT 2: %c%c%c%c%c%c%c%c ", radioheader[3], radioheader[4],
//            radioheader[5], radioheader[6], radioheader[7], radioheader[8],
//            radioheader[9], radioheader[10]);
    m_rpt1 = std::string((const char *) &radioheader[11], 8);
    m_dsdDecoder->getLogger().log("RPT 1: %s ", m_rpt1.c_str());
//    m_dsdDecoder->getLogger().log("RPT 1: %c%c%c%c%c%c%c%c ", radioheader[11], radioheader[12],
//            radioheader[13], radioheader[14], radioheader[15], radioheader[16],
//            radioheader[17], radioheader[18]);
    m_yourSign = std::string((const char *) &radioheader[19], 8);
    m_dsdDecoder->getLogger().log("YOUR: %s ", m_yourSign.c_str());
//    m_dsdDecoder->getLogger().log("YOUR: %c%c%c%c%c%c%c%c ", radioheader[19], radioheader[20],
//            radioheader[21], radioheader[22], radioheader[23], radioheader[24],
//            radioheader[25], radioheader[26]);
    m_mySign = std::string((const char *) &radioheader[27], 8);
    m_mySign += '/';
    m_mySign += std::string((const char *) &radioheader[35], 4);
    m_dsdDecoder->getLogger().log("MY: %s\n", m_mySign.c_str());
//    m_dsdDecoder->getLogger().log("MY: %c%c%c%c%c%c%c%c/%c%c%c%c\n", radioheader[27], radioheader[28],
//            radioheader[29], radioheader[30], radioheader[31], radioheader[32],
//            radioheader[33], radioheader[34], radioheader[35], radioheader[36],
//            radioheader[37], radioheader[38]);

    //FCSinheader = ((radioheader[39] << 8) | radioheader[40]) & 0xFFFF;
    //FCScalculated = calc_fcs((unsigned char*) radioheader, 39);
    //printf("Check sum = %04X ", FCSinheader);
    //if (FCSinheader == FCScalculated) {
    //  printf("(OK)\n");
    //} else {
    //  printf("(NOT OK- Calculated FCS = %04X)\n", FCScalculated);
    //}; // end else - if
}

} // namespace DSDcc
