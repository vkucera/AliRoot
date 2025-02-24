/**************************************************************************
 * Copyright(c) 1998-1999, ALICE Experiment at CERN, All rights reserved. *
 *                                                                        *
 * Author: The ALICE Off-line Project.                                    *
 * Contributors are mentioned in the code where appropriate.              *
 *                                                                        *
 * Permission to use, copy, modify and distribute this software and its   *
 * documentation strictly for non-commercial purposes is hereby granted   *
 * without fee, provided that the above copyright notice appears in all   *
 * copies and that both the copyright notice and this permission notice   *
 * appear in the supporting documentation. The authors make no claims     *
 * about the suitability of this software for any purpose. It is          *
 * provided "as is" without express or implied warranty.                  *
 **************************************************************************/

/* $Id$ */

// Event generator that using an instance of type AliGenReader
// reads particles from a file and applies cuts.
// Example: In your Config.C you can include the following lines
//  AliGenExtFile *gener = new AliGenExtFile(-1);
//  gener->SetMomentumRange(0,999);
//  gener->SetPhiRange(-180.,180.);
//  gener->SetThetaRange(0,180);
//  gener->SetYRange(-999,999);
//  AliGenReaderTreeK * reader = new AliGenReaderTreeK();
//  reader->SetFileName("myFileWithTreeK.root");
//  gener->SetReader(reader);
//  gener->Init();


#include <Riostream.h>

#include "AliLog.h"
#include "AliGenExtFile.h"
#include "AliRunLoader.h"
#include "AliHeader.h"
#include "AliStack.h"
#include "AliGenEventHeader.h"
#include "AliGenReader.h"

#include <TParticle.h>
#include <TFile.h>
#include <TTree.h>


using std::cout;
using std::endl;
using std::map;

ClassImp(AliGenExtFile)

AliGenExtFile::AliGenExtFile()
    :AliGenMC(),
     fFileName(0),
     fReader(0),
     fStartEvent(0),
     ///
     fLimitDiscardedEvents(999999),
     fSetMultTrig(0),
     fMultCut(0),
     fSetPtTrig(0),
     fPtCut(0),
     fSetUserTrig(0)
     ///
{
//  Constructor
//
//  Read all particles
    fNpart  = -1;
}

AliGenExtFile::AliGenExtFile(Int_t npart)
    :AliGenMC(npart),
     fFileName(0),
     fReader(0),
     fStartEvent(0),
     ///
     fLimitDiscardedEvents(999999),
     fSetMultTrig(0),
     fMultCut(0),
     fSetPtTrig(0),
     fPtCut(0),
     fSetUserTrig(0)
     ///
{
//  Constructor
    fName   = "ExtFile";
    fTitle  = "Primaries from ext. File";
}

//____________________________________________________________
AliGenExtFile::~AliGenExtFile()
{
// Destructor
    delete fReader;
}

//___________________________________________________________
void AliGenExtFile::Init()
{
// Initialize
    if (fReader) fReader->Init();
}

//___________________________________________________________
void AliGenExtFile::Generate()
{
// Generate particles

  Double_t polar[]  = {0,0,0};
  //
  Double_t origin[] = {0,0,0};
  Double_t time = 0.;
  Double_t p[] = {0,0,0,0};
  Float_t random[6];
  Int_t nt = 0;
  //
  //
  // Fast forward up to start Event
  for (Int_t ie=0; ie<fStartEvent; ++ie ) {
    Int_t nTracks = fReader->NextEvent();
    if (nTracks == 0) {
      // printf("\n No more events !!! !\n");
      Warning("AliGenExtFile::Generate","\nNo more events in external file!!!\nLast event may be empty or incomplete.\n");
      return;
    }
    for (Int_t i = 0; i < nTracks; ++i) {
      if (NULL == fReader->NextParticle())
	AliFatal("Error while skipping tracks");
    }
    cout << "Skipping event " << ie << endl;
  }
  fStartEvent = 0; // do not skip events the second time

  ///
  Int_t consecutiveDiscardedEvents = 0;
  ///

  while(1) {
    if (fVertexSmear == kPerEvent) Vertex();
    Int_t nTracks = fReader->NextEvent();
    if (nTracks == 0) {
      // printf("\n No more events !!! !\n");
      Warning("AliGenExtFile::Generate","\nNo more events in external file!!!\nLast event may be empty or incomplete.\n");
      return;
    }

    //
    // Particle selection loop
    //
    // The selection criterium for the external file generator is as follows:
    //
    // 1) All tracks are subject to the cuts defined by AliGenerator, i.e.
    //    fThetaMin, fThetaMax, fPhiMin, fPhiMax, fPMin, fPMax, fPtMin, fPtMax,
    //    fYMin, fYMax.
    //    If the particle does not satisfy these cuts, it is not put on the
    //    stack.
    // 2) If fCutOnChild and some specific child is selected (e.g. if
    //    fForceDecay==kSemiElectronic) the event is rejected if NOT EVEN ONE
    //    child falls into the child-cuts.
    TParticle* iparticle = 0x0;

    if(fCutOnChild) {
      // Count the selected children
      Int_t nSelected = 0;
      while ((iparticle=fReader->NextParticle()) ) {
	  Int_t kf = CheckPDGCode(iparticle->GetPdgCode());
	  kf = TMath::Abs(kf);
	  if (ChildSelected(kf) && KinematicSelection(iparticle, 1)) {
	      nSelected++;
	  }
      }
      if (!nSelected) continue;    // No particle selected:  Go to next event
      fReader->RewindEvent();
    }

    //
    // Stack selection loop
    //
    class SelectorLogic { // need to do recursive back tracking, requires a "nested" function
    private:
       Int_t idCount;
       map<Int_t,Int_t> firstMotherMap;
       map<Int_t,Int_t> secondMotherMap;
       map<Int_t,Bool_t> selectedIdMap;
       map<Int_t,Int_t> newIdMap;
       void selectMothersToo(Int_t particleId) {
	 Int_t mum1 = firstMotherMap[particleId];
          if (mum1 > -1 && !selectedIdMap[mum1]) {
             selectedIdMap[mum1] = true;
             selectMothersToo(mum1);
          }
          Int_t mum2 = secondMotherMap[particleId];
          if (mum2 > -1 && !selectedIdMap[mum2]) {
             selectedIdMap[mum2] = true;
             selectMothersToo(mum2);
          }
       }
    public:
      SelectorLogic():idCount(0), firstMotherMap(), secondMotherMap(), selectedIdMap(), newIdMap() {}
      void init() {
          idCount = 0;
       }
       void setData(Int_t id, Int_t mum1, Int_t mum2, Bool_t selected) {
          idCount++; // we know that this function is called in succession of ids, so counting is fine to determine max id
          firstMotherMap[id] = mum1;
          secondMotherMap[id] = mum2;
          selectedIdMap[id] = selected;
       }
       void reselectCuttedMothersAndRemapIDs() {
          for (Int_t id = 0; id < idCount; ++id) {
             if (selectedIdMap[id]) {
                selectMothersToo(id);
             }
          }
          Int_t newId0 = 0;
          for (Int_t id = 0; id < idCount; id++) {
             if (selectedIdMap[id]) {
                newIdMap[id] = newId0; ++newId0;
             } else {
                newIdMap[id] = -1;
             }
          }
       }
       Bool_t isSelected(Int_t id) {
          return selectedIdMap[id];
       }
       Int_t newId(Int_t id) {
          if (id == -1) return -1;
          return newIdMap[id];
       }
    };
    SelectorLogic selector;
    selector.init();
    for (Int_t i = 0; i < nTracks; i++) {
       TParticle* jparticle = fReader->NextParticle();
       if(jparticle){
	     selector.setData(i,
             jparticle->GetFirstMother(),
             jparticle->GetSecondMother(),
             KinematicSelection(jparticle,0));
    	}
       else{
	     AliWarning(Form("Particle %d = NULL",i));
       }
    }
    selector.reselectCuttedMothersAndRemapIDs();
    fReader->RewindEvent();

    //
    // Stack filling loop
    //
    fNprimaries = 0;
    AliDebugStream(3) << "Initial mother particle assignment" << std::endl;
    for (Int_t i = 0; i < nTracks; i++) {
       TParticle* jparticle = fReader->NextParticle();
       Bool_t selected = selector.isSelected(i);
       if (!selected) {
          continue;
       }
       Int_t parent = selector.newId(jparticle->GetFirstMother());
      AliDebugStream(3) << "particle " << i << " -> " << selector.newId(i) << ", with mother " << jparticle->GetFirstMother() << " -> " << parent << std::endl;

	p[0] = jparticle->Px();
	p[1] = jparticle->Py();
	p[2] = jparticle->Pz();
	p[3] = jparticle->Energy();

	Int_t idpart = jparticle->GetPdgCode();
	if(fVertexSmear==kPerTrack)
	{
	    Rndm(random,6);
	    for (Int_t j = 0; j < 3; j++) {
		origin[j]=fOrigin[j]+
		    fOsigma[j]*TMath::Cos(2*random[2*j]*TMath::Pi())*
		    TMath::Sqrt(-2*TMath::Log(random[2*j+1]));
	    }
	    Rndm(random,2);
	    time = fTimeOrigin + fOsigma[2]/TMath::Ccgs()*
	      TMath::Cos(2*random[0]*TMath::Pi())*
	      TMath::Sqrt(-2*TMath::Log(random[1]));
	} else {
	    origin[0] = fVertex[0] + jparticle->Vx();
	    origin[1] = fVertex[1] + jparticle->Vy();
	    origin[2] = fVertex[2] + jparticle->Vz();
	    time = fTime + jparticle->T();
	}
	Int_t doTracking = fTrackIt && selected && (jparticle->TestBit(kTransportBit));

	PushTrack(doTracking, parent, idpart,
		  p[0], p[1], p[2], p[3], origin[0], origin[1], origin[2], time,
		  polar[0], polar[1], polar[2],
		  kPPrimary, nt, 1., jparticle->GetStatusCode());

	KeepTrack(nt);
	fNprimaries++;
	SetHighWaterMark(nt);
    } // track loop

    // User Trigger
    if(fSetUserTrig){
      AliStack *stack = AliRunLoader::Instance()->Stack();
      if(!fUserTrigger(stack)){
	stack->Clean();
        consecutiveDiscardedEvents++;
        if(consecutiveDiscardedEvents>fLimitDiscardedEvents){AliFatal(Form("More than %i events discarded consequently",fLimitDiscardedEvents));}
        continue;
      }
      consecutiveDiscardedEvents=0;
    }

    AliStack *stack = AliRunLoader::Instance()->Stack();

    // Base multiplicity trigger
    if(fSetMultTrig){
      AliInfo(Form("mult cut : %i",fMultCut));
      if(!MultiplicityTrigger(stack)){
	stack->Clean();
        consecutiveDiscardedEvents++;
        if(consecutiveDiscardedEvents>fLimitDiscardedEvents){AliFatal(Form("More than %i events discarded consequently",fLimitDiscardedEvents));}
        continue;
      }
      consecutiveDiscardedEvents=0;
    }

    // Base pT trigger
    if(fSetPtTrig){
      AliInfo(Form("pT cut : %f GeV/c",fPtCut));
      AliStack *stack = AliRunLoader::Instance()->Stack();
      if(!PtTrigger(stack)){
	stack->Clean();
        consecutiveDiscardedEvents++;
        if(consecutiveDiscardedEvents>fLimitDiscardedEvents){AliFatal(Form("More than %i events discarded consequently",fLimitDiscardedEvents));}
        continue;
      }
      consecutiveDiscardedEvents=0;
    }
    ///

    fReader->RewindEvent();
    AliDebugStream(3) << "Final mother particle assignment" << std::endl;
    for (int iold = 0; iold < nTracks; iold++) {
      TParticle* orig_particle = fReader->NextParticle();
      if (!orig_particle) continue;
      Bool_t selected = selector.isSelected(iold);
      AliDebugStream(3) << "Particle " << iold << " with PDG = " << orig_particle->GetPdgCode() << std::endl;
      AliDebugStream(3) << orig_particle->Px() << " " << orig_particle->Py() << " " << orig_particle->Pz() << " " << orig_particle->Energy() << std::endl;
      if (!selected) {
        AliDebugStream(3) << "Particle rejected." << std::endl;
        continue;
      }
      Int_t inew = selector.newId(iold);
      TParticle* particle = stack->Particle(inew);
      if (!particle) {
        AliErrorStream() << "Could not find particle " << inew << std::endl;
        continue;
      }
      else {
        AliDebugStream(3) << "New index is " << inew << std::endl;
      }
      Int_t iparent1 = selector.newId(orig_particle->GetFirstMother());
      Int_t iparent2 = selector.newId(orig_particle->GetSecondMother());
      TParticle* parent1 = nullptr;
      TParticle* parent2 = nullptr;
      
      if (iparent1 >= 0) parent1 = stack->Particle(iparent1);
      if (iparent2 >= 0) parent2 = stack->Particle(iparent2);
      
      if (parent1 && !parent2) {
        // Only the first mother exists, nothing to be done
        AliDebugStream(3) << "Only first mother exists. Confirm parent 1 with index " << iparent1
        << " and PDG = " << parent1->GetPdgCode() << std::endl;
      }
      else if (parent2 && !parent1) {
        // Only the second mother exists
        AliDebugStream(3) << "Only second mother exists. Using mother 2 with index " << iparent2
        << " and PDG = " << parent2->GetPdgCode() << std::endl;
        particle->SetFirstMother(iparent2);
      }
      else if (parent1 && parent2) {
        // Both mothers exist. Select the mother with the larger PDG code
        AliDebugStream(3) << "Both mothers exist"
        << ". Mother 1 with index " << iparent1
        << " and PDG = " << parent1->GetPdgCode()
        << ". Mother 2 with index " << iparent2
        << " and PDG = " << parent2->GetPdgCode()
        << std::endl;
        Bool_t confirmParent1 = kTRUE;
        // The criterion is the following:
        // if one of the mother is a beam particle, prefer the other mother;
        // if one of the mother is a charm or beauty quark prefer it
        // if one of the mother is a MC special code (81-100), prefer it
        UInt_t abdPdg1 = TMath::Abs(parent1->GetPdgCode());
        UInt_t abdPdg2 = TMath::Abs(parent2->GetPdgCode());
        if (iparent1 <= 1 && iparent2 > 1) {
          AliDebugStream(3) << "Parent 1 is a beam particle." << std::endl;
          confirmParent1 = kFALSE;
        }
        else if (abdPdg2 == 5 && abdPdg1 != 5) {
          AliDebugStream(3) << "Parent 2 is a beauty quark." << std::endl;
          confirmParent1 = kFALSE;
        }
        else if (abdPdg2 == 4 && abdPdg1 != 4 && abdPdg1 != 5) {
          AliDebugStream(3) << "Parent 2 is a charm quark." << std::endl;
          confirmParent1 = kFALSE;
        }
        else if ((abdPdg2 >= 81 && abdPdg2 <= 100) && (abdPdg1 < 81 || abdPdg1 > 100)) {
          AliDebugStream(3) << "Parent 2 is a cluster." << std::endl;
          confirmParent1 = kFALSE;
        }
        if (!confirmParent1) {
          particle->SetFirstMother(iparent2);
          AliDebugStream(3) << "Using mother 2." << std::endl;
        }
        else {
          AliDebugStream(3) << "Confirming mother 1." << std::endl;
        }
      }
      else {
        AliDebugStream(3) << "No mother found." << std::endl;
      }
    }

    // Generated event header
    AliGenEventHeader * header = fReader->GetGenEventHeader();
    if (!header) header = new AliGenEventHeader();
    header->SetName(GetName());
    header->SetNProduced(fNprimaries);
    header->SetPrimaryVertex(fVertex);
    header->SetInteractionTime(fTime);
    AddHeader(header);
    break;

  } // event loop

  CdEventFile();
}

//___________________________________________________________
void AliGenExtFile::CdEventFile()
{
// CD back to the event file
  AliRunLoader::Instance()->CdGAFile();
}
//__________________________________________________________
Bool_t AliGenExtFile::MultiplicityTrigger(AliStack *stack){
  Int_t nTracks  = stack->GetNtrack();
  AliInfo(Form("n Tracks = %i",nTracks));
  if(nTracks>fMultCut){return kTRUE;}
  else{
    AliInfo(Form("n Tracks < %i --> EVENT DISCARDED",fMultCut));
    return kFALSE;
  }
}
//__________________________________________________________
Bool_t AliGenExtFile::PtTrigger(AliStack *stack){
  Int_t nTracks  = stack->GetNtrack();

  Int_t partCounter = 0;                                                        // counter of particles with pT > pT threshold
  for(int i = 0;i < nTracks;i++){
    TParticle *track = (TParticle*) stack->Particle(i);
    if(track->Pt() > fPtCut){
      AliInfo(Form("pT = %f",track->Pt()));
      partCounter++;
    }
  }

  if(partCounter>0){return kTRUE;}
  else{
    AliInfo(Form("No particle with pT > %f GeV/c --> EVENT DISCARDED",fPtCut));
    return kFALSE;
  }
}
