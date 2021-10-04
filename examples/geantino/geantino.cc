#include "G4RunManagerFactory.hh"
#include "G4UImanager.hh"
#include "G4UIExecutive.hh"
#include "G4VisExecutive.hh"
#include "FTFP_BERT.hh"

#include "G4OCCTParser.hh"

#include "PrimaryGeneratorAction.hh"
#include "DetectorConstruction.hh"
#include "ActionInitialization.hh"

int main(int argc, char **argv)
{
   if (argc < 2) {
     G4cout << "Usage: load_step <step_file> [macro_file]" << G4endl;
     return -1;
   }

   G4OCCTParser parser(argv[1]);

   auto* runManager = G4RunManagerFactory::CreateRunManager();

   runManager->SetUserInitialization(new DetectorConstruction(
                                     parser.GetWorldVolume()));
   runManager->SetUserInitialization(new FTFP_BERT);
   runManager->SetUserInitialization(new ActionInitialization());

   runManager->Initialize();

   // Initialize visualization
   G4VisManager* visManager = new G4VisExecutive;
   visManager->Initialize();

   // Get the pointer to the User Interface manager
   G4UImanager* UImanager = G4UImanager::GetUIpointer();

   if (argc == 3) {
     UImanager->ExecuteMacroFile(argv[2]);
   } else {
     G4UIExecutive* ui = new G4UIExecutive(argc, argv);
     ui->SessionStart();
     delete ui;
   }

   delete visManager;
   delete runManager;

   return 0;
}
