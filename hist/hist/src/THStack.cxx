// @(#)root/hist:$Id$
// Author: Rene Brun   10/12/2001

/*************************************************************************
 * Copyright (C) 1995-2001, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include "TROOT.h"
#include "TClassRef.h"
#include "THStack.h"
#include "TVirtualPad.h"
#include "TVirtualHistPainter.h"
#include "THashList.h"
#include "TH2.h"
#include "TH3.h"
#include "TList.h"
#include "TStyle.h"
#include "TBrowser.h"
#include "TMath.h"
#include "TObjString.h"
#include "TVirtualMutex.h"
#include "strlcpy.h"

#include <iostream>

ClassImp(THStack);

////////////////////////////////////////////////////////////////////////////////

/** \class THStack
    \ingroup Histograms
The Histogram stack class

A THStack is a collection of TH1 or TH2 histograms. By using THStack::Draw(), the entire
histogram collection is drawn at once according to the specified drawing option.

THStack::Add() allows adding a new histogram to the list. Note that the THStack does not
take ownership of the objects in the list.

\anchor HS00
### Stack painting

By default, histograms are shown stacked:
  - the first histogram is painted
  - then the sum of the first and second histograms is painted, and so on

The axis ranges are computed automatically along the X and Y axes to display the complete
histogram collection.

Warning: Histogram bins with negative content may produce wrong plots.

### Stack's drawing options

The specific stack's drawing options are:

  - **NOSTACK** If option "nostack" is specified, histograms are all painted in the same pad
    as if the option "same" had been specified.

  - **NOSTACKB** If the option "nostackb" is specified histograms are all painted on the same pad
    next to each other as bar plots.

  - **PADS** if option "pads" is specified, the current pad/canvas is subdivided into
    a number of pads equal to the number of histograms and each histogram
    is painted into a separate pad.

  - **NOCLEAR** By default the background of the histograms is erased before drawing the
    histograms. The option "noclear" avoids this behavior. This is useful when drawing a
    THStack on top of another plot. If the patterns used to draw the histograms in the
    stack are transparent, then the plot behind will be visible.

See the THistPainter class for the list of valid histograms' painting options.


Example;

Begin_Macro(source)
{
   auto hs = new THStack("hs","");
   auto h1 = new TH1F("h1","test hstack",10,-4,4);
   h1->FillRandom("gaus",20000);
   h1->SetFillColor(kRed);
   hs->Add(h1);
   auto h2 = new TH1F("h2","test hstack",10,-4,4);
   h2->FillRandom("gaus",15000);
   h2->SetFillColor(kBlue);
   hs->Add(h2);
   auto h3 = new TH1F("h3","test hstack",10,-4,4);
   h3->FillRandom("gaus",10000);
   h3->SetFillColor(kGreen);
   hs->Add(h3);
   auto cs = new TCanvas("cs","cs",10,10,700,900);
   TText T; T.SetTextFont(42); T.SetTextAlign(21);
   cs->Divide(2,2);
   cs->cd(1); hs->Draw(); T.DrawTextNDC(.5,.95,"Default drawing option");
   cs->cd(2); hs->Draw("nostack"); T.DrawTextNDC(.5,.95,"Option \"nostack\"");
   cs->cd(3); hs->Draw("nostackb"); T.DrawTextNDC(.5,.95,"Option \"nostackb\"");
   cs->cd(4); hs->Draw("lego1"); T.DrawTextNDC(.5,.95,"Option \"lego1\"");
}
End_Macro

A more complex example:

Begin_Macro(source)
../../../tutorials/hist/hstack.C
End_Macro

Note that picking is supported for all drawing modes.

\since **ROOT version 6.07/07:**
Stacks of 2D histograms can also be painted as candle plots:
\since **ROOT version 6.09/02:**
Stacks of 2D histograms can also be painted as violin plots, combinations of candle and
violin plots are possible as well:

Begin_Macro(source)
../../../tutorials/hist/candleplotstack.C
End_Macro

Automatic coloring according to the current palette is available as shown in the
following example:

Begin_Macro(source)
../../../tutorials/hist/thstackpalettecolor.C
End_Macro
*/


////////////////////////////////////////////////////////////////////////////////
/// constructor with name and title

THStack::THStack(const char *name, const char *title)
       : TNamed(name,title)
{
   R__LOCKGUARD(gROOTMutex);
   gROOT->GetListOfCleanups()->Add(this);
}


////////////////////////////////////////////////////////////////////////////////
/// Creates a new THStack from a TH2 or TH3.
/// It is filled with the 1D histograms from GetProjectionX or GetProjectionY
/// for each bin of the histogram. It illustrates the differences and total
/// sum along an axis.
///
/// Parameters:
/// - hist:  the histogram used for the projections. Can be an object deriving
///          from TH2 or TH3.
/// - axis:  for TH2: "x" for ProjectionX, "y" for ProjectionY.
///          for TH3: see TH3::Project3D.
/// - name:  fName is set to name if given, otherwise to histo's name with
///          "_stack_<axis>" appended, where `<axis>` is the value of the
///          parameter axis.
/// - title: fTitle is set to title if given, otherwise to histo's title
///          with ", stack of <axis> projections" appended.
/// - firstbin, lastbin:
///          For each bin within [firstbin,lastbin] a stack entry is created.
///          See TH2::ProjectionX/Y for use overflow bins.
///          Defaults to "all bins but under- / overflow"
/// - firstbin2, lastbin2:
///          Other axis range for TH3::Project3D, defaults to "all bins but
///          under- / overflow". Ignored for TH2s
/// - proj_option:
///          option passed to TH2::ProjectionX/Y and TH3::Project3D (along
///          with axis)
/// - draw_option:
///          option passed to THStack::Add.

THStack::THStack(TH1* hist, Option_t *axis /*="x"*/,
                 const char *name /*=nullptr*/, const char *title /*=nullptr*/,
                 Int_t firstbin /*=1*/, Int_t lastbin /*=-1*/,
                 Int_t firstbin2 /*=1*/, Int_t lastbin2 /*=-1*/,
                 Option_t* proj_option /*=""*/, Option_t* draw_option /*=""*/)
     : TNamed(name, title) {
   {
      R__LOCKGUARD(gROOTMutex);
      gROOT->GetListOfCleanups()->Add(this);
   }
   if (!axis) {
      Warning("THStack", "Need an axis.");
      return;
   }
   if (!hist) {
      Warning("THStack", "Need a histogram.");
      return;
   }
   Bool_t isTH2 = hist->IsA()->InheritsFrom(TH2::Class());
   Bool_t isTH3 = hist->IsA()->InheritsFrom(TH3::Class());
   if (!isTH2 && !isTH3) {
      Warning("THStack", "Need a histogram deriving from TH2 or TH3.");
      return;
   }

   if (!fName.Length())
      fName.Form("%s_stack%s", hist->GetName(), axis);
   if (!fTitle.Length()) {
      if (hist->GetTitle() && strlen(hist->GetTitle()))
         fTitle.Form("%s, stack of %s projections", hist->GetTitle(), axis);
      else
         fTitle.Form("stack of %s projections", axis);
   }

   if (isTH2) {
      TH2* hist2 = (TH2*) hist;
      Bool_t useX = (strchr(axis,'x')) || (strchr(axis,'X'));
      Bool_t useY = (strchr(axis,'y')) || (strchr(axis,'Y'));
      if ((!useX && !useY) || (useX && useY)) {
         Warning("THStack", "Need parameter axis=\"x\" or \"y\" for a TH2, not none or both.");
         return;
      }
      TAxis* haxis = useX ? hist->GetYaxis() : hist->GetXaxis();
      if (!haxis) {
         Warning("THStack","Histogram axis is NULL");
         return;
      }
      Int_t nbins = haxis->GetNbins();
      if (firstbin < 0) firstbin = 1;
      if (lastbin  < 0) lastbin  = nbins;
      if (lastbin  > nbins+1) lastbin = nbins;
      for (Int_t iBin=firstbin; iBin<=lastbin; iBin++) {
         TH1* hProj = nullptr;
         if (useX)
            hProj = hist2->ProjectionX(TString::Format("%s_px%d",hist2->GetName(), iBin).Data(),
                                          iBin, iBin, proj_option);
         else
            hProj = hist2->ProjectionY(TString::Format("%s_py%d",hist2->GetName(), iBin).Data(),
                                         iBin, iBin, proj_option);
         Add(hProj, draw_option);
      }
   } else {
      // hist is a TH3
      TH3* hist3 = (TH3*) hist;
      TString sAxis(axis);
      sAxis.ToLower();
      Int_t dim=3-sAxis.Length();
      if (dim<1 || dim>2) {
         Warning("THStack", "Invalid length for parameter axis.");
         return;
      }

      if (dim==1) {
         TAxis* haxis = nullptr;
         // look for the haxis _not_ in axis
         if (sAxis.First('x')==kNPOS)
            haxis = hist->GetXaxis();
         else if (sAxis.First('y')==kNPOS)
            haxis = hist->GetYaxis();
         else if (sAxis.First('z')==kNPOS)
            haxis = hist->GetZaxis();
         if (!haxis) {
            Warning("THStack","Histogram axis is NULL");
            return;
         }

         Int_t nbins = haxis->GetNbins();
         if (firstbin < 0) firstbin = 1;
         if (lastbin  < 0) lastbin  = nbins;
         if (lastbin  > nbins+1) lastbin = nbins;
         Int_t iFirstOld=haxis->GetFirst();
         Int_t iLastOld=haxis->GetLast();
         for (Int_t iBin=firstbin; iBin<=lastbin; iBin++) {
            haxis->SetRange(iBin, iBin);
            // build projection named axis_iBin (passed through "option")
            TH1* hProj = hist3->Project3D(TString::Format("%s_%s%s_%d", hist3->GetName(),
                                             axis, proj_option, iBin).Data());
            Add(hProj, draw_option);
         }
         haxis->SetRange(iFirstOld, iLastOld);
      }  else {
         // if dim==2
         TAxis* haxis1 = nullptr;
         TAxis* haxis2 = nullptr;
         // look for the haxis _not_ in axis
         if (sAxis.First('x')!=kNPOS) {
            haxis1=hist->GetYaxis();
            haxis2=hist->GetZaxis();
         } else if (sAxis.First('y')!=kNPOS) {
            haxis1=hist->GetXaxis();
            haxis2=hist->GetZaxis();
         } else if (sAxis.First('z')!=kNPOS) {
            haxis1=hist->GetXaxis();
            haxis2=hist->GetYaxis();
         }
         if (!haxis1 || !haxis2) {
            Warning("THStack","Histogram axis is NULL");
            return;
         }

         Int_t nbins1 = haxis1->GetNbins();
         Int_t nbins2 = haxis2->GetNbins();
         if (firstbin < 0) firstbin = 1;
         if (lastbin  < 0) lastbin  = nbins1;
         if (lastbin  > nbins1+1) lastbin = nbins1;
         if (firstbin2 < 0) firstbin2 = 1;
         if (lastbin2  < 0) lastbin2  = nbins2;
         if (lastbin2  > nbins2+1) lastbin2 = nbins2;
         Int_t iFirstOld1 = haxis1->GetFirst();
         Int_t iLastOld1 = haxis1->GetLast();
         Int_t iFirstOld2 = haxis2->GetFirst();
         Int_t iLastOld2 = haxis2->GetLast();
         for (Int_t iBin=firstbin; iBin<=lastbin; iBin++) {
            haxis1->SetRange(iBin, iBin);
            for (Int_t jBin=firstbin2; jBin<=lastbin2; jBin++) {
               haxis2->SetRange(jBin, jBin);
               // build projection named axis_iBin (passed through "option")
               TH1* hProj=hist3->Project3D(TString::Format("%s_%s%s_%d", hist3->GetName(),
                                                axis, proj_option, iBin).Data());
               Add(hProj, draw_option);
            }
         }
         haxis1->SetRange(iFirstOld1, iLastOld1);
         haxis2->SetRange(iFirstOld2, iLastOld2);
      }
   } // if hist is TH2 or TH3
}

////////////////////////////////////////////////////////////////////////////////
/// THStack destructor.

THStack::~THStack()
{
   {
      R__LOCKGUARD(gROOTMutex);
      gROOT->GetListOfCleanups()->Remove(this);
   }
   if (fHists) {
      fHists->Clear("nodelete");
      delete fHists;
      fHists = nullptr;
   }
   if (fStack) {
      fStack->Delete();
      delete fStack;
      fStack = nullptr;
   }
   delete fHistogram;
   fHistogram = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// THStack copy constructor

THStack::THStack(const THStack &hstack) :
   TNamed(hstack),
   fMaximum(hstack.fMaximum),
   fMinimum(hstack.fMinimum)
{
   {
      R__LOCKGUARD(gROOTMutex);
      gROOT->GetListOfCleanups()->Add(this);
   }

   TIter next(hstack.GetHists());
   while (auto h = static_cast<TH1 *>(next()))
      Add(h);
}

////////////////////////////////////////////////////////////////////////////////
/// Add a new histogram to the list.
/// Only 1-d and 2-d histograms currently supported.
/// A drawing option may be specified

void THStack::Add(TH1 *h1, Option_t *option)
{
   if (!h1) return;
   if (h1->GetDimension() > 2) {
      Error("Add","THStack supports only 1-d and 2-d histograms");
      return;
   }
   if (!fHists) fHists = new TList();
   fHists->Add(h1,option);
   Modified(); //invalidate stack
}

////////////////////////////////////////////////////////////////////////////////
/// Browse.

void THStack::Browse(TBrowser *b)
{
   Draw(b ? b->GetDrawOption() : "");
   gPad->Update();
}

////////////////////////////////////////////////////////////////////////////////
/// Build the sum of all histograms.
/// Build a separate list fStack containing the running sum of all histograms

void THStack::BuildStack()
{
   if (fStack) return;
   if (!fHists) return;
   Int_t nhists = fHists->GetSize();
   if (!nhists) return;
   fStack = new TObjArray(nhists);
   Bool_t add = TH1::AddDirectoryStatus();
   TH1::AddDirectory(kFALSE);
   TH1 *h = (TH1*)fHists->At(0)->Clone();
   fStack->Add(h);
   for (Int_t i=1;i<nhists;i++) {
      h = (TH1*)fHists->At(i)->Clone();
      if (h->GetMinimum() < 0.) {
         Warning("BuildStack","Histograms with a negative minimum may produce wrong plots");
      }
      h->Add((TH1*)fStack->At(i-1));
      fStack->AddAt(h,i);
   }
   TH1::AddDirectory(add);
}

////////////////////////////////////////////////////////////////////////////////
/// Compute distance from point px, py to each graph.

Int_t THStack::DistancetoPrimitive(Int_t px, Int_t py)
{
   //*-*- Are we on the axis?
   const Int_t kMaxDiff = 10;
   Int_t distance = 9999;
   if (fHistogram) {
      distance = fHistogram->DistancetoPrimitive(px,py);
      if (distance <= 0) {return distance;}
      if (distance <= 1) {gPad->SetSelected(fHistogram);return distance;}
   }


   //*-*- Loop on the list of histograms
   if (!fHists) return distance;
   const char *doption = GetDrawOption();
   Int_t nhists = fHists->GetSize();
   for (Int_t i=0;i<nhists;i++) {
      TH1 *h = (TH1*)fHists->At(i);
      if (fStack && !strstr(doption,"nostack")) h = (TH1*)fStack->At(i);
      Int_t dist = h->DistancetoPrimitive(px,py);
      if (dist <= 0) return 0;
      if (dist < kMaxDiff) {
         gPad->SetSelected(fHists->At(i));
         gPad->SetCursor(kPointer);
         return dist;
      }
   }
   return distance;
}

////////////////////////////////////////////////////////////////////////////////
/// Draw this stack with its current attributes.
///
/// Options to draw histograms  are described in THistPainter::Paint
/// By default (if the option "nostack" is not specified), histograms will be painted
/// stacked on top of each other.

void THStack::Draw(Option_t *option)
{
   TString opt = option;
   opt.ToLower();
   if (gPad) {
      if (!gPad->IsEditable()) gROOT->MakeDefCanvas();
      if (!opt.Contains("same")) {
         //the following statement is necessary in case one attempts to draw
         //a temporary histogram already in the current pad
         if (TestBit(kCanDelete)) gPad->GetListOfPrimitives()->Remove(this);
         gPad->Clear();
      }
   }
   AppendPad(opt.Data());
}

////////////////////////////////////////////////////////////////////////////////
/// Returns a pointer to the histogram used to draw the axis.
/// Takes into account the two following cases:
///   1- option 'A' was specified in THStack::Draw. Return fHistogram
///   2- user had called TPad::DrawFrame. return pointer to hframe histogram
///
/// IMPORTANT NOTES
/// - You must call Draw before calling this function. The returned histogram
///   depends on the selected Draw options.
/// - This function returns a pointer to an intermediate fixed bin size
///   histogram used to set the range and for picking.
///   You cannot use this histogram to return the bin information.
///   You must get a pointer to one of the histograms in the stack,
///   the first one, for example.

TH1 *THStack::GetHistogram() const
{
   if (fHistogram) return fHistogram;
   if (!gPad) return nullptr;
   gPad->Modified();
   gPad->Update();
   if (fHistogram) return fHistogram;
   return (TH1*)gPad->FindObject("hframe");
}

////////////////////////////////////////////////////////////////////////////////
/// Returns the maximum of all added histograms smaller than maxval.
/// Returns the maximum of all histograms, smaller than maxval, if option "nostack".

Double_t THStack::GetMaximum(Option_t *option, Double_t maxval)
{
   TString opt = option;
   opt.ToLower();
   Bool_t lerr = kFALSE;
   if (opt.Contains("e")) lerr = kTRUE;
   Double_t them = 0, themax = -std::numeric_limits<Double_t>::max(), c1, e1;
   if (!fHists) return 0;
   Int_t nhists = fHists->GetSize();
   TH1 *h;
   Int_t first,last;

   if (!opt.Contains("nostack")) {
      BuildStack();
      h = (TH1*)fStack->At(nhists-1);
      if (fHistogram) h->GetXaxis()->SetRange(fHistogram->GetXaxis()->GetFirst(),
                                              fHistogram->GetXaxis()->GetLast());
      themax = h->GetMaximum(maxval);
   } else {
      for (Int_t i=0;i<nhists;i++) {
         h = (TH1*)fHists->At(i);
         if (fHistogram) h->GetXaxis()->SetRange(fHistogram->GetXaxis()->GetFirst(),
                                                 fHistogram->GetXaxis()->GetLast());
         them = h->GetMaximum(maxval);
         if (fHistogram) h->GetXaxis()->SetRange(0,0);
         if (them > themax) themax = them;
      }
   }

   if (lerr) {
      for (Int_t i=0;i<nhists;i++) {
         h = (TH1*)fHists->At(i);
         first = h->GetXaxis()->GetFirst();
         last  = h->GetXaxis()->GetLast();
         for (Int_t j=first; j<=last;j++) {
            e1     = h->GetBinError(j);
            c1     = h->GetBinContent(j);
            themax = TMath::Max(themax,c1+e1);
         }
      }
   }

   return themax;
}

////////////////////////////////////////////////////////////////////////////////
/// Returns the minimum of all added histograms larger than minval.
/// Returns the minimum of all histograms, larger than minval, if option "nostack".

Double_t THStack::GetMinimum(Option_t *option, Double_t minval)
{
   TString opt = option;
   opt.ToLower();
   Bool_t lerr = kFALSE;
   if (opt.Contains("e")) lerr = kTRUE;
   Double_t them = 0, themin = std::numeric_limits<Double_t>::max(), c1, e1;
   if (!fHists) return 0;
   Int_t nhists = fHists->GetSize();
   Int_t first,last;
   TH1 *h;

   if (!opt.Contains("nostack")) {
      BuildStack();
      h = (TH1*)fStack->At(nhists-1);
      themin = h->GetMinimum(minval);
   } else {
      for (Int_t i=0;i<nhists;i++) {
         h = (TH1*)fHists->At(i);
         them = h->GetMinimum(minval);
         if (them <= 0 && gPad && gPad->GetLogy()) them = h->GetMinimum(0);
         if (them < themin) themin = them;
      }
   }

   if (lerr) {
      for (Int_t i=0;i<nhists;i++) {
         h = (TH1*)fHists->At(i);
         first = h->GetXaxis()->GetFirst();
         last  = h->GetXaxis()->GetLast();
         for (Int_t j=first; j<=last;j++) {
             e1     = h->GetBinError(j);
             c1     = h->GetBinContent(j);
             themin = TMath::Min(themin,c1-e1);
         }
      }
   }

   return themin;
}

////////////////////////////////////////////////////////////////////////////////
/// Return the number of histograms in the stack

Int_t THStack::GetNhists() const
{
   if (fHists) return fHists->GetSize();
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Return pointer to Stack. Build it if not yet done.

TObjArray *THStack::GetStack()
{
   BuildStack();
   return fStack;
}

////////////////////////////////////////////////////////////////////////////////
/// Get the x-axis of the histogram used to draw the stack.
///
/// IMPORTANT NOTE
///  You must call Draw before calling this function. The returned histogram
///  depends on the selected Draw options.

TAxis *THStack::GetXaxis() const
{
   TH1 *h = GetHistogram();
   return h ? h->GetXaxis() : nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// Get the y-axis of the histogram used to draw the stack.
///
/// IMPORTANT NOTE
///  You must call Draw before calling this function. The returned histogram
///  depends on the selected Draw options.

TAxis *THStack::GetYaxis() const
{
   TH1 *h = GetHistogram();
   return h ? h->GetYaxis() : nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// Get the z-axis of the histogram used to draw the stack.
///
/// IMPORTANT NOTE
///  You must call Draw before calling this function. The returned histogram
///  depends on the selected Draw options.

TAxis *THStack::GetZaxis() const
{
   TH1 *h = GetHistogram();
   if (!h) return nullptr;
   if (h->GetDimension() == 1)
      Warning("GetZaxis","1D Histograms don't have a Z axis");
   return h->GetZaxis();
}

////////////////////////////////////////////////////////////////////////////////
/// List histograms in the stack.

void THStack::ls(Option_t *option) const
{
   TROOT::IndentLevel();
   std::cout <<IsA()->GetName()
      <<" Name= "<<GetName()<<" Title= "<<GetTitle()<<" Option="<<option<<std::endl;
   TROOT::IncreaseDirLevel();
   if (fHists) fHists->ls(option);
   TROOT::DecreaseDirLevel();
}
////////////////////////////////////////////////////////////////////////////////
/// Merge the THStack in the TList into this stack.
/// Returns the total number of histograms in the result or -1 in case of an error.

Long64_t THStack::Merge(TCollection* li, TFileMergeInfo * /* info */)
{
   if (li==nullptr || li->GetEntries()==0) {
      return fHists->GetEntries();
   }
   TIter next(li);
   TList histLists;
   while (TObject* o = next()) {
      THStack *stack = dynamic_cast<THStack*> (o);
      if (!stack) {
         Error("Merge",
               "Cannot merge - an object which doesn't inherit from THStack found in the list");
         return -1;
      }
      histLists.Add(stack->GetHists());
   }
   fHists->Merge(&histLists);
   return fHists->GetEntries();
}

////////////////////////////////////////////////////////////////////////////////
/// Note: this method invalidates the sum of histograms.

void THStack::Modified()
{
   if (!fStack) return;
   fStack->Delete();
   delete fStack;
   fStack = nullptr;
   delete fHistogram;
   fHistogram = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// [Paint the list of histograms.](#HS00)

void THStack::Paint(Option_t *chopt)
{
   BuildAndPaint(chopt, kTRUE);
}

////////////////////////////////////////////////////////////////////////////////
/// Create all additional objects and stack (if specified).

void THStack::BuildAndPaint(Option_t *choptin, Bool_t paint)
{
   if (!fHists) return;
   if (!fHists->GetSize()) return;

   char option[128];
   strlcpy(option,choptin,128);

   // Automatic color
   char *l1 = strstr(option,"pfc"); // Automatic Fill Color
   char *l2 = strstr(option,"plc"); // Automatic Line Color
   char *l3 = strstr(option,"pmc"); // Automatic Marker Color
   if (l1 || l2 || l3) {
      TString opt1 = option;
      if (l1) memcpy(l1,"   ",3);
      if (l2) memcpy(l2,"   ",3);
      if (l3) memcpy(l3,"   ",3);
      TString ws = option;
      if (ws.IsWhitespace()) strncpy(option,"\0",1);
      Int_t nhists = fHists->GetSize();
      gPad->IncrementPaletteColor(nhists, opt1);
      for (Int_t i = 0; i < nhists; i++) {
         auto ic = gPad->NextPaletteColor();
         auto hAti = static_cast<TH1 *>(fHists->At(i));
         if (l1) hAti->SetFillColor(ic);
         if (l2) hAti->SetLineColor(ic);
         if (l3) hAti->SetMarkerColor(ic);
         if (fStack) {
            auto hsAti = static_cast<TH1 *>(fStack->At(i));
            if (l1) hsAti->SetFillColor(ic);
            if (l2) hsAti->SetLineColor(ic);
            if (l3) hsAti->SetMarkerColor(ic);
         }
      }
   }

   TString opt = option;
   opt.ToLower();
   opt.ReplaceAll(" ","");
   Bool_t lsame = kFALSE;
   if (opt.Contains("same")) {
      lsame = kTRUE;
      opt.ReplaceAll("same","");
   }
   Bool_t lclear = kTRUE;
   if (opt.Contains("noclear")) {
      lclear = kFALSE;
      opt.ReplaceAll("noclear","");
   }
   if (opt.Contains("pads")) {
      Int_t npads = fHists->GetSize();
      TVirtualPad *padsav = gPad;
      //if pad is not already divided into subpads, divide it
      Int_t nps = 0;
      TIter nextp(padsav->GetListOfPrimitives());
      while (auto obj = nextp()) {
         if (obj->InheritsFrom(TVirtualPad::Class())) nps++;
      }
      if (nps < npads) {
         padsav->Clear();
         Int_t nx = (Int_t)TMath::Sqrt((Double_t)npads);
         if (nx*nx < npads) nx++;
         Int_t ny = nx;
         if (((nx*ny)-nx) >= npads) ny--;
         padsav->Divide(nx,ny);

         Int_t i = 0;
         auto lnk = fHists->FirstLink();
         while (lnk) {
            i++;
            padsav->cd(i);
            lnk->GetObject()->Draw(lnk->GetOption());
            lnk = lnk->Next();
         }
         padsav->cd();
      }
      return;
   }

   // compute the min/max of each axis
   TH1 *h;
   TIter next(fHists);
   Double_t xmin = 1e100;
   Double_t xmax = -xmin;
   Double_t ymin = 1e100;
   Double_t ymax = -xmin;
   while ((h=(TH1*)next())) {
      // in case of automatic binning
      if (h->GetBuffer()) h->BufferEmpty(-1);
      if (h->GetXaxis()->GetXmin() < xmin) xmin = h->GetXaxis()->GetXmin();
      if (h->GetXaxis()->GetXmax() > xmax) xmax = h->GetXaxis()->GetXmax();
      if (h->GetYaxis()->GetXmin() < ymin) ymin = h->GetYaxis()->GetXmin();
      if (h->GetYaxis()->GetXmax() > ymax) ymax = h->GetYaxis()->GetXmax();
   }

   TString loption = opt;
   Bool_t nostack  = loption.Contains("nostack");
   Bool_t nostackb = loption.Contains("nostackb");
   Bool_t candle   = loption.Contains("candle");
   Bool_t violin   = loption.Contains("violin");

   // do not delete the stack. Another pad may contain the same object
   // drawn in stack mode!
   //if (nostack && fStack) {fStack->Delete(); delete fStack; fStack = 0;}

   if (!nostack && !candle && !violin) BuildStack();

   Double_t themax,themin;
   if (fMaximum == -1111) themax = GetMaximum(option);
   else                   themax = fMaximum;
   if (fMinimum == -1111) {
      themin = GetMinimum(option);
      if (gPad->GetLogy()){
         if (themin>0)  themin *= .9;
         else           themin = themax*1.e-3;
      }
      else if (themin > 0)
         themin = 0;
   }
   else                   themin = fMinimum;
   if (!fHistogram) {
      Bool_t add = TH1::AddDirectoryStatus();
      TH1::AddDirectory(kFALSE);
      h = (TH1*)fHists->At(0);
      TAxis *xaxis = h->GetXaxis();
      TAxis *yaxis = h->GetYaxis();
      const TArrayD *xbins = xaxis->GetXbins();
      if (h->GetDimension() > 1) {
         if (loption.IsNull()) loption = "lego1";
         const TArrayD *ybins = yaxis->GetXbins();
         if (xbins->fN != 0 && ybins->fN != 0) {
            fHistogram = new TH2F(GetName(),GetTitle(),
               xaxis->GetNbins(), xbins->GetArray(),
               yaxis->GetNbins(), ybins->GetArray());
         } else if (xbins->fN != 0 && ybins->fN == 0) {
            fHistogram = new TH2F(GetName(),GetTitle(),
               xaxis->GetNbins(), xbins->GetArray(),
               yaxis->GetNbins(), ymin, ymax);
         } else if (xbins->fN == 0 && ybins->fN != 0) {
            fHistogram = new TH2F(GetName(),GetTitle(),
               xaxis->GetNbins(), xmin, xmax,
               yaxis->GetNbins(), ybins->GetArray());
         } else {
            fHistogram = new TH2F(GetName(),GetTitle(),
               xaxis->GetNbins(), xmin, xmax,
               yaxis->GetNbins(), ymin, ymax);
         }
      } else {
         if (xbins->fN != 0) {
            fHistogram = new TH1F(GetName(),GetTitle(),
                                  xaxis->GetNbins(), xbins->GetArray());
         } else {
            fHistogram = new TH1F(GetName(),GetTitle(),xaxis->GetNbins(),xmin, xmax);
         }
      }
      fHistogram->SetStats(false);
      TH1::AddDirectory(add);
   } else {
      fHistogram->SetTitle(GetTitle());
   }

   if (nostackb) {
      loption.ReplaceAll("nostackb","");
   } else {
      if (nostack) loption.ReplaceAll("nostack","");
      fHistogram->GetPainter()->SetStack(fHists);
   }

   if (!fHistogram->TestBit(TH1::kIsZoomed)) {
      if (nostack && fMaximum != -1111) fHistogram->SetMaximum(fMaximum);
      else {
         if (gPad->GetLogy())           fHistogram->SetMaximum(themax*(1+0.2*TMath::Log10(themax/themin)));
         else {
            if (fMaximum != -1111)      fHistogram->SetMaximum(themax);
            else                        fHistogram->SetMaximum((1+gStyle->GetHistTopMargin())*themax);
         }
      }
      if (nostack && fMinimum != -1111) fHistogram->SetMinimum(fMinimum);
      else {
         if (gPad->GetLogy())           fHistogram->SetMinimum(themin/(1+0.5*TMath::Log10(themax/themin)));
         else                           fHistogram->SetMinimum(themin);
      }
   }

   // Copy the axis labels if needed.
   TH1 *hfirst = (TH1*)fHists->First();
   THashList* labels = hfirst->GetXaxis()->GetLabels();
   if (labels) {
      TIter iL(labels);
      Int_t ilab = 1;
      while (auto lb=(TObjString*)iL()) {
         fHistogram->GetXaxis()->SetBinLabel(ilab,lb->String().Data());
         ilab++;
      }
   }

   // Set fHistogram attributes and paint it.
   if (!lsame) {
      fHistogram->SetLineWidth(0);
      if (paint)
         fHistogram->Paint(loption.Data());
   }

   if (fHistogram->GetDimension() > 1)
      SetDrawOption(loption.Data());
   if (loption.Index("lego")>=0)
      return;

   char noption[32];
   strlcpy(noption,loption.Data(),32);
   Int_t nhists = fHists->GetSize();
   if (nostack || candle || violin) {
      auto lnk = fHists->FirstLink();
      Double_t bo = 0.03;
      Double_t bw = (1.-(2*bo))/nhists;
      for (Int_t i=0;i<nhists;i++) {
         if (strstr(lnk->GetOption(),"same")) {
            if (nostackb) loption.Form("%s%s b",noption,lnk->GetOption());
            else          loption.Form("%s%s",noption,lnk->GetOption());
         } else {
            TString indivOpt = lnk->GetOption();
            indivOpt.ToLower();
            if (nostackb) loption.Form("%ssame%s b",noption,lnk->GetOption());
            else if (candle && (indivOpt.Contains("candle") || indivOpt.Contains("violin"))) loption.Form("%ssame",lnk->GetOption());
            else          loption.Form("%ssame%s",noption,lnk->GetOption());
         }
         TH1* hAti = (TH1*) fHists->At(i);
         if (nostackb) {
            hAti->SetBarWidth(bw);
            hAti->SetBarOffset(bo);
            bo += bw;
         }
         if (candle || violin) {
            float candleSpace = 1./(nhists*2);
            float candleOffset = - 1./2 + candleSpace + 2*candleSpace*i;
            candleSpace *= 1.66; //width of the candle per bin: 1.0 means space is as great as the candle, 2.0 means there is no space
            hAti->SetBarWidth(candleSpace);
            hAti->SetBarOffset(candleOffset);
         }
         if (paint)
            hAti->Paint(loption.Data());
         lnk = lnk->Next();
      }
   } else {
      auto lnk = fHists->LastLink();
      Int_t h1col, h1fill;
      for (Int_t i=0;i<nhists;i++) {
         if (strstr(lnk->GetOption(),"same")) {
            loption.Form("%s%s",noption,lnk->GetOption());
         } else {
            loption.Form("%ssame%s",noption,lnk->GetOption());
         }
         TH1 *h1 = (TH1*) fStack->At(nhists-i-1);
         if ((i > 0) && lclear && paint) {
            // Erase before drawing the histogram
            h1col  = h1->GetFillColor();
            h1fill = h1->GetFillStyle();
            h1->SetFillColor(10);
            h1->SetFillStyle(1001);
            h1->Paint(loption.Data());
            static TClassRef clTFrame = TClass::GetClass("TFrame", kFALSE);
            TAttFill *frameFill = (TAttFill*)clTFrame->DynamicCast(TAttFill::Class(),gPad->GetFrame());
            if (frameFill) {
               h1->SetFillColor(frameFill->GetFillColor());
               h1->SetFillStyle(frameFill->GetFillStyle());
            }
            h1->Paint(loption.Data());
            h1->SetFillColor(h1col);
            h1->SetFillStyle(h1fill);
         }
         if (paint)
            h1->Paint(loption.Data());
         lnk = lnk->Prev();
      }
   }

   opt.ReplaceAll("nostack","");
   opt.ReplaceAll("candle","");
   if (!lsame && !opt.Contains("a") && paint)
      fHistogram->Paint("axissame");
}

////////////////////////////////////////////////////////////////////////////////
/// Print the list of histograms

void THStack::Print(Option_t *option) const
{
   TIter   next(fHists);
   while (auto h = next())
      h->Print(option);
}

////////////////////////////////////////////////////////////////////////////////
/// Recursively remove the object `obj` from the list of histograms.

void THStack::RecursiveRemove(TObject *obj)
{
   if (!fHists) return;
   fHists->RecursiveRemove(obj);
   while (fHists->IndexOf(obj) >= 0) fHists->Remove(obj);
}

////////////////////////////////////////////////////////////////////////////////
/// Save primitive as a C++ statement(s) on output stream out.

void THStack::SavePrimitive(std::ostream &out, Option_t *option /*= ""*/)
{
   char quote = '"';
   out<<"   "<<std::endl;
   if (gROOT->ClassSaved(THStack::Class())) {
      out<<"   ";
   } else {
      out<<"   THStack *";
   }
   out<<GetName()<<" = new THStack();"<<std::endl;
   out<<"   "<<GetName()<<"->SetName("<<quote<<GetName()<<quote<<");"<<std::endl;
   out<<"   "<<GetName()<<"->SetTitle("<<quote<<GetTitle()<<quote<<");"<<std::endl;

   if (fMinimum != -1111) {
      out<<"   "<<GetName()<<"->SetMinimum("<<fMinimum<<");"<<std::endl;
   }
   if (fMaximum != -1111) {
      out<<"   "<<GetName()<<"->SetMaximum("<<fMaximum<<");"<<std::endl;
   }

   static Int_t frameNumber = 0;
   if (fHistogram) {
      frameNumber++;
      TString hname = fHistogram->GetName();
      fHistogram->SetName(TString::Format("%s_stack_%d", hname.Data(), frameNumber).Data());
      fHistogram->SavePrimitive(out,"nodraw");
      out<<"   "<<GetName()<<"->SetHistogram("<<fHistogram->GetName()<<");"<<std::endl;
      out<<"   "<<std::endl;
      fHistogram->SetName(hname.Data()); // restore histogram name
   }

   if (fHists) {
      auto lnk = fHists->FirstLink();
      Int_t hcount = 0;
      while (lnk) {
         auto h = (TH1 *) lnk->GetObject();
         TString hname = h->GetName();
         h->SetName(TString::Format("%s_stack_%d", hname.Data(), ++hcount).Data());
         h->SavePrimitive(out,"nodraw");
         out<<"   "<<GetName()<<"->Add("<<h->GetName()<<","<<quote<<lnk->GetOption()<<quote<<");"<<std::endl;
         lnk = lnk->Next();
         h->SetName(hname.Data()); // restore histogram name
      }
   }
   out<<"   "<<GetName()<<"->Draw("<<quote<<option<<quote<<");"<<std::endl;
}

////////////////////////////////////////////////////////////////////////////////
/// Set maximum.

void THStack::SetMaximum(Double_t maximum)
{
   fMaximum = maximum;
   if (fHistogram) fHistogram->SetMaximum(maximum);
}

////////////////////////////////////////////////////////////////////////////////
/// Set minimum.

void THStack::SetMinimum(Double_t minimum)
{
   fMinimum = minimum;
   if (fHistogram) fHistogram->SetMinimum(minimum);
}


////////////////////////////////////////////////////////////////////////////////
/// Get an iterator over internal hists list.
TIter THStack::begin() const
{
   return TIter(fHists);
}
